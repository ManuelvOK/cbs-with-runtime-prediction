#include "task.h"

#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <sys/syscall.h>

#include <cmath>
#include <ctime>
#include <functional>
#include <iostream>
#include <sstream>

using namespace std::chrono_literals;
using time_point = std::chrono::time_point<std::chrono::steady_clock>;
using duration = typename std::chrono::nanoseconds;

time_point Task::process_start;

static time_point thread_now();

#define gettid() syscall(__NR_gettid)

#define SCHED_DEADLINE       6

/* XXX use the proper syscall numbers */
#ifdef __x86_64__
#define __NR_sched_setattr           314
#define __NR_sched_getattr           315
#endif

#ifdef __i386__
#define __NR_sched_setattr           351
#define __NR_sched_getattr           352
#endif

#ifdef __arm__
#define __NR_sched_setattr           380
#define __NR_sched_getattr           381
#endif

int sched_setattr(pid_t pid,
                  const struct sched_attr *attr,
                  unsigned int flags)
{
    return syscall(__NR_sched_setattr, pid, attr, flags);
}

int sched_getattr(pid_t pid,
                  struct sched_attr *attr,
                  unsigned int size,
                  unsigned int flags)
{
    return syscall(__NR_sched_getattr, pid, attr, size, flags);
}

struct sched_attr {
    __u32 size;

    __u32 sched_policy;
    __u64 sched_flags;

    /* SCHED_NORMAL, SCHED_BATCH */
    __s32 sched_nice;

    /* SCHED_FIFO, SCHED_RR */
    __u32 sched_priority;

    /* SCHED_DEADLINE (nsec) */
    __u64 sched_runtime;
    __u64 sched_deadline;
    __u64 sched_period;
};



Task::Task(int id, duration execution_time, duration period)
    : _id(id), _sem(0), _execution_time(execution_time), _period(period) {
        this->_thread = std::thread(&Task::run_task, this);
}

void Task::run_task() {
    this->_pid = gettid();

    /* execute all tasks on CPU 0 */
    cpu_set_t set;
    CPU_SET(0,&set);

    int ret = sched_setaffinity(0, sizeof(set), &set);
    if (ret < 0) {
        perror("sched_setaffinity");
        exit(-1);
    }

    /* configure deadline scheduling */
    struct sched_attr attr;
    unsigned int flags = 0;

    attr.size = sizeof(attr);
    attr.sched_flags = 0;
    attr.sched_nice = 0;
    attr.sched_priority = 0;

    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_runtime = this->_execution_time / 1ns;
    attr.sched_period = attr.sched_deadline = this->_period / 1ns;

    ret = sched_setattr(0, &attr, flags);
    if (ret < 0) {
        perror("sched_setattr");
        exit(-1);
    }

    /* run jobs if there are some */
    while (true) {
        this->_sem.acquire();
        if (this->_jobs.empty()) {
            this->_running = false;
            break;
        }
        this->run_job();
        this->_next_job++;
        this->_n_jobs_waiting--;
    }
}

void Task::run_job() {
    /* get jobs parameters */
    Job job = this->_jobs.front();
    this->_jobs.pop();

    time_point t_begin = std::chrono::steady_clock::now();
    time_point t_end = t_begin;
    time_point thread_begin = thread_now();

    std::stringstream event;
    event << "b " << t_begin.time_since_epoch() / 1us << " " << job._id;
    this->_events.push_back(event.str());

    this->_result = 1.5;
    for (int i = 0; i < job._execution_time / 1us * 10; ++i) {
        this->_result *= std::exp(this->_result * std::exp(this->_result * std::exp(this->_result)));
    }

    t_end = std::chrono::steady_clock::now();
    time_point thread_end = thread_now();

    event = std::stringstream("");
    event << "e " << t_end.time_since_epoch() / 1us << " " << job._id
          << " " << (thread_end - thread_begin) / 1us;
    this->_events.push_back(event.str());
}

void Task::join() {
    this->_thread.join();
}

void Task::write_back_events() {
    std::cout << this->_id << " p " << this->_pid << std::endl;
    for (std::string event: this->_events) {
        std::cout << this->_id << " " << event << std::endl;
    }
}

time_point thread_now() {
    using rep = typename std::chrono::nanoseconds::rep;

    struct timespec cputime;

    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cputime);

    rep ns = static_cast<rep>(cputime.tv_nsec) +
             static_cast<rep>(cputime.tv_sec) * 1'000'000'000;
    return time_point(duration(ns));
}

