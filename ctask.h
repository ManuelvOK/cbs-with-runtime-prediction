#pragma once

struct metrics {
    int size;
    double *data;
};

#ifdef __cplusplus
extern "C" {
#endif

int create_non_rt_task(int cpus, int id, void (*execute)(void *));

int create_task(int cpus, int id, int period, void (*execute)(void *), int execution_time);

int create_task_with_prediction(int cpus, int id, int period, void (*execute)(void *), struct metrics(*generate)(void *));

void add_job_to_task(int task, void *arg);

void join_task(int task);

int task_id(int task);

void release_sem(int task);

int task_period(int task);

#ifdef __cplusplus
}
#endif
