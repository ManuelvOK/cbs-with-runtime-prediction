//#include <unistd.h>
//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//#include <time.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <semaphore>
#include <sstream>
#include <thread>
#include <vector>

#include "rt.h"
#include "sched_sim_tracepoint.h"
#include "task.h"

using namespace std::chrono_literals;
using time_point = std::chrono::time_point<std::chrono::steady_clock>;
using duration = typename std::chrono::nanoseconds;

struct Job {
    int _id;
    duration _execution_time;
    time_point _deadline;
    time_point _submission_time;
    int _task_id;
};

using SimTask = Task<Job>;

void wait_busily(Job job) {
    time_point thread_end = thread_now() + job._execution_time;
    while (thread_now() < thread_end) {
        /* spin */
    }
}

struct Model {
    std::map<int, SimTask*> _tasks;
    std::vector<Job> _jobs;
    int _n_cores = 1;
    bool _prediction_enabled = false;

    time_point _start = time_point(0us);

    void add_task(SimTask *task) {
        this->_tasks[task->id()] = task;
    }

    void calculate_deadlines() {
        std::map<int, std::vector<Job *>> tasks_jobs;
        for (Job &j: this->_jobs) {
            tasks_jobs[j._task_id].push_back(&j);
        }
        time_point zero(0us);
        for (auto &[task_id, jobs]: tasks_jobs) {
            if (not this->_tasks.contains(task_id)) {
                std::cerr << "Input Error: unresolvable task id: " << task_id << std::endl;
                exit(EXIT_FAILURE);
            }
            SimTask *task = this->_tasks[task_id];
            int i = 1;
            for (Job *job: jobs) {
                job->_deadline = zero + (task->period() * i);
                job->_id = i - 1;
                //std::cerr << "calculate job " << i << " of task " << task_id << ". submission: "
                //          << job->_submission_time.time_since_epoch() / 1us << ". deadline: "
                //          << job->_deadline.time_since_epoch() / 1us << ". execution_time: "
                //          << job->_execution_time / 1us << std::endl;
                ++i;
            }
        }
    }

    void set_start_time(time_point start) {
        this->_start = start;
        for (Job &job: this->_jobs) {
            job._deadline += start.time_since_epoch();
            job._submission_time += start.time_since_epoch();
        }
    }

    void sort_jobs() {
        std::sort(this->_jobs.begin(), this->_jobs.end(),
                  [](const Job &a, const Job &b){
                      return a._submission_time < b._submission_time;
                  });
    }
};

static Job parse_job(std::stringstream *ss) {
    int id;
    int execution_time;
    int submission_time;
    int task_id;
    *ss >> id >> execution_time >> submission_time >> task_id;
    return Job(id, execution_time * 1us, time_point{0us}, time_point{submission_time * 1us},
               task_id);
}

static SimTask *parse_task(std::stringstream *ss, Model *model) {
    int id;
    int execution_time;
    int period;
    *ss >> id >> execution_time >> period;
    return new SimTask(id, model->_prediction_enabled, period * 1us, wait_busily);
}

static void parse_line(std::string line, Model *model) {
    std::stringstream ss(line);
    char type = ' ';
    /* first char in each line specifies type of line to parse */
    ss >> type;
    switch (type) {
        break; case 'c': ss >> model->_n_cores;
        break; case 'j': model->_jobs.push_back(parse_job(&ss));
        break; case 'S': model->add_task(parse_task(&ss, model));
        break; case ' ':
        break; case 0:
        break; case '#':
        break; default: std::cerr << "Parse error: \"" << type << "\" is not a proper type."
                                  << std::endl;
                        exit(EXIT_FAILURE);
    }
}

static struct Model parse_input(std::string path, bool prediction_enabled) {
    std::ifstream input_file(path);
    if (not input_file.is_open()) {
        std::cerr << "Could not open file: " << path << std::endl;
        exit(EXIT_FAILURE);
    }

    Model model;
    model._prediction_enabled = prediction_enabled;
    std::string line;
    while (std::getline(input_file, line)) {
        parse_line(line, &model);
    }
    input_file.close();

    model.calculate_deadlines();

    return model;
}

int main(int argc, char *argv[]) {
    lttng_ust_tracepoint(sched_sim, start_main);

    /* put the job spawning onto CPU 1 */
    cpu_set_t set;
    CPU_SET(1,&set);

    int ret;
    ret = sched_setaffinity(0, sizeof(set), &set);
    if (ret < 0) {
        perror("sched_setaffinity");
        exit(-1);
    }

    lttng_ust_tracepoint(sched_sim, migrated, 1);

    /* configure deadline scheduling */
    struct sched_attr attr;
    unsigned int flags = 0;

    attr.size = sizeof(attr);
    attr.sched_flags = 0;
    attr.sched_nice = 0;
    attr.sched_priority = SCHED_FIFO;

    attr.sched_policy = SCHED_FIFO;

    ret = sched_setattr(0, &attr, flags);
    if (ret < 0) {
        perror("sched_setattr");
        exit(-1);
    }

    if (argc <= 1) {
        std::cerr << "no input file provided. Exiting." << std::endl;
        exit(1);
    }

    bool prediction_enabled = false;
    if (argc > 2 && std::string(argv[2]) == "1") {
        prediction_enabled = true;
    }

    Model model = parse_input(argv[1], prediction_enabled);

    lttng_ust_tracepoint(sched_sim, input_parsed);

    /* Allow tasks to initialise */
    std::this_thread::sleep_for(3ms);

    lttng_ust_tracepoint(sched_sim, waited_for_task_init);

    /* wait at least one period for every task */
    duration initial_wait =
        std::max_element(model._tasks.begin(), model._tasks.end(),
                         [](std::pair<int, SimTask *> a, std::pair<int, SimTask *> b) {
                             return a.second->period() < b.second->period();
                         })->second->period();
    model.set_start_time(std::chrono::steady_clock::now() + initial_wait);

    /* spawn jobs */
    model.sort_jobs();
    time_point now = std::chrono::steady_clock::now();
    for (Job &job: model._jobs) {

        if (job._submission_time - now > 1ms) {
            //std::cerr << "sleep_until " << job._submission_time.time_since_epoch() / 1us << std::endl;
            std::this_thread::sleep_until(job._submission_time - 1ms);
            //std::cerr << "slept. Its now " << std::chrono::steady_clock::now().time_since_epoch() / 1us << std::endl;
        }
        /* busy wait if next spawn is in future */
        while (job._submission_time > now) {
            now = std::chrono::steady_clock::now();
        }

        /* spawn job */
        SimTask *task = model._tasks[job._task_id];
        lttng_ust_tracepoint(sched_sim, job_spawn, task->id(), job._id, (job._deadline - now.time_since_epoch()).time_since_epoch() / 1ns);

        task->add_job(job);
        task->sem().release();
    }

    for (auto &[_, task]: model._tasks) {
        task->sem().release();
        task->join();
    }

    return 0;
}

