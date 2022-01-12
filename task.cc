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

std::chrono::time_point<std::chrono::steady_clock> Task::process_start;

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



Task::Task(int id, int execution_time, int period, int n_jobs)
    : _id(id), _sem(0), _execution_time(execution_time), _period(period), _n_jobs(n_jobs) {
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
    /* log start */
    //auto now = std::chrono::steady_clock::now();

    /* configure deadline scheduling */
    struct sched_attr attr;
    unsigned int flags = 0;

    attr.size = sizeof(attr);
    attr.sched_flags = 0;
    attr.sched_nice = 0;
    attr.sched_priority = 0;

    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_runtime = (this->_execution_time + 1) * 1000 * 1000;
    attr.sched_period = attr.sched_deadline = this->_period * 1000 * 1000;

    ret = sched_setattr(0, &attr, flags);
    if (ret < 0) {
        perror("sched_setattr");
        exit(-1);
    }

    /* run jobs if there are some */
    while (true) {
        if (this->_next_job == this->_n_jobs) {
            this->_running = false;
            break;
        }
        this->_sem.acquire();
        this->run_job();
        this->_next_job++;
        this->_n_jobs_waiting--;
    }

    /* log stop */
    //now = std::chrono::steady_clock::now();
    //this->_log_file << (now - Task::process_start).count() / 1000 << ": stopping task" << std::endl;
}

void Task::run_job() {
    auto t_begin = std::chrono::steady_clock::now();
    auto t_end = t_begin;
    time_point thread_begin = thread_now();

    std::stringstream message;
    message << "starting job " << this->_next_job;
    this->_events.emplace(t_begin.time_since_epoch().count() / 1000, message.str());

    this->_result = 1.5;
    for (int i = 0; i < this->_execution_time * 1000 * 100; ++i) {
        this->_result *= this->_result * this->_result;
    }

    t_end = std::chrono::steady_clock::now();
    time_point thread_end = thread_now();
    message = std::stringstream("");
    message << "finished job " << this->_next_job
            << ". Runtime " << (thread_end - thread_begin).count() / 1000;
    this->_events.emplace(t_end.time_since_epoch().count() / 1000, message.str());
}

void Task::join() {
    this->_thread.join();
}

void Task::write_back_events() {
    std::cout << this->_id << ":pid:" << this->_pid << std::endl;
    for (auto &[time, event]: this->_events) {
        std::cout << this->_id << ":" << time << ":" << event << std::endl;
    }
}

time_point thread_now() {
    using rep = typename std::chrono::nanoseconds::rep;
    using duration = typename std::chrono::nanoseconds;

    struct timespec cputime;

    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cputime);

    rep ns = static_cast<rep>(cputime.tv_nsec) +
             static_cast<rep>(cputime.tv_sec) * 1'000'000'000;
    return time_point(duration(ns));
}

