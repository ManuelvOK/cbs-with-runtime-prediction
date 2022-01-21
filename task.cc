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

#include "sched_sim_tracepoint.h"

using namespace std::chrono_literals;
using time_point = std::chrono::time_point<std::chrono::steady_clock>;
using duration = typename std::chrono::nanoseconds;

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
    attr.sched_runtime = this->_execution_time / 1ns;
    attr.sched_period = attr.sched_deadline = this->_period / 1ns;

    ret = sched_setattr(0, &attr, flags);
    if (ret < 0) {
        perror("sched_setattr");
        std::cerr << "runtime: " << attr.sched_runtime << std::endl;
        std::cerr << "period: " << attr.sched_period << std::endl;
        exit(-1);
    }

    lttng_ust_tracepoint(sched_sim, started_real_time_task, this->_id);
    sched_yield();

    /* run jobs if there are some */
    while (true) {
        lttng_ust_tracepoint(sched_sim, acquire_sem, this->_id);
        this->_sem.acquire();
        lttng_ust_tracepoint(sched_sim, acquired_sem, this->_id);

        if (this->_jobs.empty()) {
            this->_running = false;
            lttng_ust_tracepoint(sched_sim, finished_task, this->_id);
            break;
        }
        this->run_job();
    }
}

void Task::run_job() {
    /* get jobs parameters */
    Job job = this->_jobs.front();
    this->_jobs.pop();

    time_point thread_begin = thread_now();

    lttng_ust_tracepoint(sched_sim, begin_job, this->_id, job._id);

    time_point thread_end = thread_begin + job._execution_time - 5us;
    while (thread_now() < thread_end) {
        /* spin */
    }

    lttng_ust_tracepoint(sched_sim, end_job, this->_id, job._id, (thread_now() - thread_begin) / 1ns);
}

void Task::join() {
    this->_thread.join();
}

time_point thread_now() {
    using rep = typename std::chrono::nanoseconds::rep;

    struct timespec cputime;

    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cputime);

    rep ns = static_cast<rep>(cputime.tv_nsec) +
             static_cast<rep>(cputime.tv_sec) * 1'000'000'000;
    return time_point(duration(ns));
}

