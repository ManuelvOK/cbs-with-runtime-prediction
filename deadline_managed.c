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

static volatile int done;

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

void *run_deadline(void *data)
{
    struct sched_attr attr;
    int x = 0;
    int ret;
    unsigned int flags = 0;

     cpu_set_t set;
     CPU_SET(0,&set);

     ret = sched_setaffinity(0, sizeof(set), &set);
     if (ret < 0) {
         done = 0;
         perror("sched_setaffinity");
         exit(-1);
     }

    printf("deadline thread started [%ld]\n", gettid());

    attr.size = sizeof(attr);
    attr.sched_flags = 0;
    attr.sched_nice = 0;
    attr.sched_priority = 0;

    /* This creates a 10ms/30ms reservation */
    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_runtime = 10 * 1000 * 1000;
    attr.sched_period = attr.sched_deadline = 30 * 1000 * 1000;

    ret = sched_setattr(0, &attr, flags);
    if (ret < 0) {
        done = 0;
        perror("sched_setattr");
        exit(-1);
    }

    while (!done) {
        x++;
    }

    printf("deadline thread dies [%ld]\n", x, gettid());
    return NULL;
}

struct thread_data {
    sem_t *sem;
    float execution_time;
    float period;
    int running;
};

void *run_task(void *data) {

    int ret;

    cpu_set_t set;
    CPU_SET(0,&set);

    ret = sched_setaffinity(0, sizeof(set), &set);
    if (ret < 0) {
        done = 0;
        perror("sched_setaffinity");
        exit(-1);
    }

    struct thread_data *t_data = data;
    struct timespec tstart={0,0};
    struct timespec tend={0,0};
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    float fstart = tstart.tv_sec + 1.0e-9*tstart.tv_nsec;
    printf("%.5f: starting task as deadline thread\n", fstart);

    struct sched_attr attr;
    unsigned int flags = 0;

    attr.size = sizeof(attr);
    attr.sched_flags = 0;
    attr.sched_nice = 0;
    attr.sched_priority = 0;

    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_runtime = t_data->execution_time * 1000 * 1000;
    attr.sched_period = attr.sched_deadline = t_data->period * 1000 * 1000;

    ret = sched_setattr(0, &attr, flags);
    if (ret < 0) {
        done = 0;
        perror("sched_setattr");
        exit(-1);
    }

    while (t_data->running) {
        sem_wait(t_data->sem);
        clock_gettime(CLOCK_MONOTONIC, &tstart);
        fstart = tstart.tv_sec + 1.0e-9*tstart.tv_nsec;
        printf("%.5f: starting job\n", fstart);
        while (1) {
            clock_gettime(CLOCK_MONOTONIC, &tend);
            float interval = (tend.tv_sec + 1.0e-9*tend.tv_nsec) - (tstart.tv_sec + 1.0e-9*tstart.tv_nsec);
            if (interval > t_data->execution_time / 1000.0) {
                printf("%.5f: job finished\n", fstart + interval);
                break;
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    fstart = tstart.tv_sec + 1.0e-9*tstart.tv_nsec;
    printf("%.5f: stopping task\n", fstart);
}

int main (int argc, char **argv)
{
    sem_t sem1;
    sem_init(&sem1,0,0);
    sem_t sem2;
    sem_init(&sem2,0,0);

    struct thread_data data1 = {.sem = &sem1, .execution_time = 20, .period = 40, .running = 1};
    struct thread_data data2 = {.sem = &sem2, .execution_time = 20, .period = 40, .running = 1};

    pthread_t thread1;
    pthread_create(&thread1, NULL, run_task, &data1);

    pthread_t thread2;
    pthread_create(&thread2, NULL, run_task, &data2);

    struct timespec delay={0,data1.period * 1000 * 1000};
    for (int i = 0; i < 5; ++i) {
        sem_post(&sem1);
        sem_post(&sem2);
        nanosleep(&delay, NULL);
    }
    data1.running = 0;
    sem_post(&sem1);

    data2.running = 0;
    sem_post(&sem2);

    done = 1;
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    return 0;
}

