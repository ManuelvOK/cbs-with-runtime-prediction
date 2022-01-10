//#include <unistd.h>
//#include <stdio.h>
//#include <stdlib.h>
//#include <string.h>
//#include <time.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <semaphore>
#include <thread>
#include <vector>

#include "task.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))


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

    Task::process_start = std::chrono::high_resolution_clock::now();
    /* initialise tasks */
    std::vector<Task *> tasks;
    tasks.emplace_back(new Task{0, 10, 40, 20});
    tasks.emplace_back(new Task{1, 10, 30, 25});

    std::chrono::duration<double> sleep_time(0.02);
    std::this_thread::sleep_for(sleep_time);
    /* spawn jobs */
    int now = 0;
    while (true) {
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
        int time_to_next_spawn = next_task->_next_period - now;
        if (time_to_next_spawn) {
            std::chrono::duration<double> sleep_time((time_to_next_spawn - 0.5) / 1000.0);
            std::this_thread::sleep_for(sleep_time);
            now = next_task->_next_period;
        }

        /* spawn job */
        auto now = std::chrono::high_resolution_clock::now();
        std::cout << (now - Task::process_start).count() / 1000
                  << ": spawning job " << next_task->_next_job + next_task->_n_jobs_waiting
                  << " for task " << next_task->_id
                  << std::endl;
        next_task->_n_jobs_waiting++;
        next_task->_next_period += next_task->_period;
        next_task->_sem.release();
    }

    for (Task *task: tasks) {
        task->join();
    }

    for (Task *t: tasks) {
        t->write_back_events();
        delete t;
    }

    return 0;
}

