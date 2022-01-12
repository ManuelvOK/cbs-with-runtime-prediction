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

#include "task.h"

using namespace std::chrono_literals;
using time_point = std::chrono::time_point<std::chrono::steady_clock>;
using duration = typename std::chrono::nanoseconds;

int main (void) {
    /* put the job spawning onto CPU 1 */
    cpu_set_t set;
    CPU_SET(1,&set);

    int ret;
    ret = sched_setaffinity(0, sizeof(set), &set);
    if (ret < 0) {
        perror("sched_setaffinity");
        exit(-1);
    }

    time_point start = std::chrono::steady_clock::now();
    Task::process_start = start;
    /* initialise tasks */
    std::vector<Task *> tasks;
    tasks.emplace_back(new Task{0, 100us, 400us});
    tasks.emplace_back(new Task{1, 150us, 600us});

    std::map<int, std::vector<std::string>> events;

    std::this_thread::sleep_for(1us);

    start = std::chrono::steady_clock::now();
    std::cerr << "start: " << start.time_since_epoch().count() << std::endl;

    for (Task *t: tasks) {
        t->_next_period = start;
    }

    /* spawn jobs */
    time_point now = start;
    for (int i = 0; i < 50; ++i) {
        /* find next task to spawn job */
        Task *next_task = *std::min_element(tasks.begin(), tasks.end(),
            [](Task *a, Task *b) {
                if (not a->_running) {
                    return false;
                }
                return a->_next_period < b->_next_period;
            }
        );

        /* break if all tasks finished */
        if (not next_task->_running) {
            break;
        }

        /* sleep if next spawn is in future */
        duration time_to_next_spawn = next_task->_next_period - now;
        if (time_to_next_spawn > 1ns) {
            std::this_thread::sleep_until(next_task->_next_period);
        }

        /* spawn job */
        now = std::chrono::steady_clock::now();
        int job_id = next_task->_next_job + next_task->_n_jobs_waiting;
        duration job_execution_time = next_task->_execution_time;
        time_point job_deadline = now + next_task->_period;

        std::stringstream event;
        event << "s " << now.time_since_epoch() / 1us << " " << job_id
              << " " << job_deadline.time_since_epoch() / 1us;
        events[next_task->_id].push_back(event.str());
        next_task->_next_period += next_task->_period;
        next_task->_n_jobs_waiting++;

        next_task->_jobs.emplace(job_id, job_execution_time, job_deadline);
        next_task->_sem.release();
    }

    for (Task *task: tasks) {
        task->_sem.release();
        task->join();
    }

    for (auto &[task_id, es]: events) {
        for (std::string e: es) {
            std::cout << task_id << " " << e << std::endl;
        }
    }

    for (Task *t: tasks) {
        t->write_back_events();
        delete t;
    }

    return 0;
}

