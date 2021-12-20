#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <semaphore.h>

#define gettid() syscall(__NR_gettid)

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

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

struct thread_data {
    sem_t *sem;
    float execution_time;
    float period;
    int running;
    int next_period;
};

void *run_task(void *data) {

    int ret;
    int id = gettid();

    cpu_set_t set;
    CPU_SET(0,&set);

    ret = sched_setaffinity(0, sizeof(set), &set);
    if (ret < 0) {
        perror("sched_setaffinity");
        exit(-1);
    }

    struct thread_data *t_data = data;
    struct timespec tstart={0,0};
    struct timespec tend={0,0};
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    float fstart = tstart.tv_sec + 1.0e-9*tstart.tv_nsec;
    printf("%.5f: starting task as deadline thread [%ld]\n", fstart, id);

    struct sched_attr attr;
    unsigned int flags = 0;

    attr.size = sizeof(attr);
    attr.sched_flags = 0;
    attr.sched_nice = 0;
    attr.sched_priority = 0;

    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_runtime = (t_data->execution_time + 1) * 1000 * 1000;
    attr.sched_period = attr.sched_deadline = t_data->period * 1000 * 1000;

    ret = sched_setattr(0, &attr, flags);
    if (ret < 0) {
        perror("sched_setattr");
        exit(-1);
    }

    while (t_data->running) {
        sem_wait(t_data->sem);
        clock_gettime(CLOCK_MONOTONIC, &tstart);
        fstart = tstart.tv_sec + 1.0e-9*tstart.tv_nsec;
        printf("%.5f: starting job [%ld]\n", fstart, id);
        while (1) {
            clock_gettime(CLOCK_MONOTONIC, &tend);
            float interval = (tend.tv_sec + 1.0e-9*tend.tv_nsec) - (tstart.tv_sec + 1.0e-9*tstart.tv_nsec);
            if (interval > t_data->execution_time / 1000.0) {
                printf("%.5f: job finished [%ld]\n", fstart + interval, id);
                break;
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    fstart = tstart.tv_sec + 1.0e-9*tstart.tv_nsec;
    printf("%.5f: stopping task [%ld]\n", fstart, id);
}

#define N_TASKS 2

int main (int argc, char **argv)
{

    cpu_set_t set;
    CPU_SET(1,&set);

    int ret;
    ret = sched_setaffinity(0, sizeof(set), &set);
    if (ret < 0) {
        perror("sched_setaffinity");
        exit(-1);
    }
    const int n_tasks = N_TASKS;
    sem_t sems[n_tasks];

    struct thread_data data[N_TASKS] = {{.sem = &sems[0], .execution_time = 10, .period = 40,
                                         .running = 1},
                                        {.sem = &sems[1], .execution_time = 10, .period = 30,
                                         .running = 1}};

    pthread_t threads[n_tasks];
    for (int i = 0; i < n_tasks; ++i) {
        sem_init(&sems[i],0,0);
        pthread_create(&threads[i], NULL, run_task, &data[i]);
    }

    int now = 0;
    int next_period = 0;
    while (now < 250) {
        for (int i = 0; i < n_tasks; ++i) {
            if (now == data[i].next_period) {
                sem_post(&sems[i]);
                data[i].next_period += data[i].period;
            }
            if (next_period == now) {
                next_period = data[i].next_period;
            } else {
                next_period = MIN(next_period, data[i].next_period);
            }
        }
        struct timespec delay={0,(next_period - now) * 1000 * 1000};
        nanosleep(&delay, NULL);
        now = next_period;
    }

    for (int i = 0; i < n_tasks; ++i) {
        data[i].running = 0;
        sem_post(&sems[i]);
        pthread_join(threads[i], NULL);
    }
    return 0;
}

