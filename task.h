#pragma once

#include <chrono>
#include <functional>
#include <fstream>
#include <iostream>
#include <semaphore>
#include <thread>
#include <queue>

#include <predictor/predictor.h>

#include "rt.h"
#include "sched_sim_tracepoint.h"


using namespace std::chrono_literals;
using time_point = std::chrono::time_point<std::chrono::steady_clock>;
using duration = typename std::chrono::nanoseconds;

time_point thread_now() {
    using rep = typename std::chrono::nanoseconds::rep;
    struct timespec cputime;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cputime);
    rep ns = static_cast<rep>(cputime.tv_nsec) +
        static_cast<rep>(cputime.tv_sec) * 1'000'000'000;
    return time_point(duration(ns));
}


class TaskBase {
  protected:
    int _id;
    bool _prediction_enabled;
    std::counting_semaphore<> _sem;
    duration _execution_time;
    duration _period;

    time_point _last_checkpoint;
    std::vector<double> _runtimes;
    atlas::estimator _predictor;

    std::thread _thread;
    bool _running = true;
    int _pid = 0;
    double _result = 1.5;

    void run_task() {
        this->_pid = gettid();
        lttng_ust_tracepoint(sched_sim, init_task, this->_id, this->_pid);

        /* execute all tasks on CPU 0 */
        cpu_set_t set;
        CPU_SET(0,&set);

        int ret = sched_setaffinity(0, sizeof(set), &set);
        if (ret < 0) {
            perror("sched_setaffinity");
            exit(-1);
        }

        lttng_ust_tracepoint(sched_sim, migrated_task, this->_id, 0);

        /* configure deadline scheduling */
        struct sched_attr attr;
        unsigned int flags = 0;

        attr.size = sizeof(attr);
        attr.sched_flags = 0;
        attr.sched_nice = 0;
        attr.sched_priority = 0;

        attr.sched_policy = SCHED_DEADLINE;
        if (this->_execution_time > 1us) {
            attr.sched_runtime = this->_execution_time / 1ns;
        } else {
            attr.sched_runtime = (0.9*this->_period) / 1ns;
        }
        attr.sched_period = attr.sched_deadline = this->_period / 1ns;

        ret = sched_setattr(0, &attr, flags);
        if (ret < 0) {
            perror("initial sched_setattr");
            std::cerr << "runtime: " << attr.sched_runtime << std::endl;
            std::cerr << "period: " << attr.sched_period << std::endl;
            exit(-1);
        }

        lttng_ust_tracepoint(sched_sim, started_real_time_task, this->_id);
        sched_yield();

        /* run jobs if there are some */
        int job_id = 0;
        while (true) {
            lttng_ust_tracepoint(sched_sim, acquire_sem, this->_id);
            this->_sem.acquire();
            lttng_ust_tracepoint(sched_sim, acquired_sem, this->_id);

            if (not this->jobs_left()) {
                this->_running = false;
                lttng_ust_tracepoint(sched_sim, finished_task, this->_id);
                break;
            }
            this->run_job(job_id);
            job_id++;
        }
    }

    virtual void run_job(int id) = 0;

    virtual bool jobs_left() = 0;

  public:
    TaskBase(int id, bool prediction_enabled, duration execution_time, duration period)
        : _id(id), _prediction_enabled(prediction_enabled), _sem(0),
          _execution_time(execution_time), _period(period) {
            this->_thread = std::thread(&TaskBase::run_task, this);
        }

    void join() {
        this->_thread.join();
    }

    int id() const {
        return this->_id;
    }

    std::counting_semaphore<> &sem() {
        return this->_sem;
    }

    duration period() const {
        return this->_period;
    }
};

template <typename T>
class Task : public TaskBase {
    std::function<std::vector<double> (T)> _generate;
    std::function<void (T)> _execute;
    std::queue<T> _jobs;

    void run_job(int id) override {
        /* get jobs parameters */
        T arg = this->_jobs.front();
        this->_jobs.pop();
        if (this->_prediction_enabled) {
            std::vector<double> metrics = this->_generate(arg);
            duration prediction  = this->_predictor.predict(0, id, metrics.data(), metrics.size());
            /* first prediction is always 90% of the period. It will most likely not take this time
             * but we make sure to get the first measurement asap. 90% is already configured at
             * initialisation if prediction is enabled, so here goes only the first checkpoint */
            if (not this->_runtimes.size()) {
                this->_last_checkpoint = thread_now();
            } else {

                lttng_ust_tracepoint(sched_sim, prediction, this->_id, id, prediction / 1ns);

                /* configure deadline scheduling */
                struct sched_attr attr;
                sched_getattr(gettid(), &attr, sizeof(attr), 0);

                attr.sched_runtime = prediction / 1ns;
                attr.sched_runtime = std::min(attr.sched_runtime, attr.sched_period);

                int ret = sched_setattr(0, &attr, 0);
                if (ret < 0) {
                    perror("job sched_setattr");
                    std::cerr << "runtime: " << attr.sched_runtime << std::endl;
                    std::cerr << "period: " << attr.sched_period << std::endl;
                    exit(-1);
                }
            }
        }

        lttng_ust_tracepoint(sched_sim, begin_job, this->_id, id);

        this->_execute(arg);

        time_point now = thread_now();
        auto runtime = now - this->_last_checkpoint;
        this->_last_checkpoint = now;

        this->_runtimes.push_back(runtime / 1ns);
        if (this->_prediction_enabled) {
            this->_predictor.train(0, id, duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::duration<double>{runtime} + 0.5ns));
        }
        lttng_ust_tracepoint(sched_sim, end_job, this->_id, id, runtime / 1ns);
        if (this->_prediction_enabled and this->_runtimes.size() == 1) {
            sched_yield();
        }
    }

    bool jobs_left() override {
        return not this->_jobs.empty();
    };

  public:
    Task(int id, bool prediction_enabled, duration period, std::function<void (T)> execute,
         duration execution_time)
        : TaskBase(id, prediction_enabled, execution_time, period), _execute(execute) {}

    Task(int id, bool prediction_enabled, duration period, std::function<void (T)> execute)
        : TaskBase(id, prediction_enabled, duration(0), period), _execute(execute) {
            this->_generate = [](T t) { (void)t; return std::vector<double>(); };
        }

    Task(int id, bool prediction_enabled, duration period, std::function<void (T)> execute,
         std::function<std::vector<double> (T)> generate)
        : TaskBase(id, prediction_enabled, -1, period), _execute(execute), _generate(generate) {}

    void add_job(T arg) {
        this->_jobs.push(arg);
    }
};
