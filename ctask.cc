#include "task.h"
#include "ctask.h"

#include <functional>
#include <vector>

using namespace std::chrono_literals;
using time_point = std::chrono::time_point<std::chrono::steady_clock>;
using duration = typename std::chrono::nanoseconds;

std::vector<Task<void *> *> tasks;

static std::vector<unsigned> get_cpus(int cpus) {
    std::vector<unsigned> ret;
    for (int i = 0; i < 8; ++i) {
        if (cpus % 2) {
            ret.push_back(cpus % 2);
        }
        cpus = cpus >> 1;
    }
    return ret;
}

static std::vector<double> generate_metrics(struct metrics(*generate)(void *), void *data) {
    std::vector<double> metrics;
    if (generate == nullptr) {
        return metrics;
    }

    struct metrics metrics_struct = generate(data);
    for (int i = 0; i < metrics_struct.size; ++i) {
        metrics.push_back(metrics_struct.data[i]);
    }
    delete metrics_struct.data;
    return metrics;
}

int create_task(int cpus, int id, int period, void (*execute)(void *), int execution_time) {
    Task<void *> *task = new Task<void *>(id, duration(period), std::function<void(void *)>(execute), duration(execution_time), get_cpus(cpus));
    int handle = tasks.size();
    tasks.push_back(task);
    return handle;
}

int create_task_with_prediction(int cpus, int id, int period, void (*execute)(void *), struct metrics(*generate)(void *)) {
    auto gen_metrics = std::bind(generate_metrics, generate, std::placeholders::_1);
    Task<void *> *task = new Task<void *>(id, true, duration(period), std::function<void(void *)>(execute), gen_metrics, get_cpus(cpus));
    int handle = tasks.size();
    tasks.push_back(task);
    return handle;
}

void add_job_to_task(int task, void *arg) {
    tasks[task]->add_job(arg);
}

void join_task(int task) {
    tasks[task]->join();
}

int task_id(int task) {
    return tasks[task]->id();
}

void release_sem(int task) {
    tasks[task]->sem().release();
}

int task_period(int task) {
    return tasks[task]->period() / 1ns;
}
