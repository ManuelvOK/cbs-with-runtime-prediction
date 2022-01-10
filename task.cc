#include "task.h"

#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <sys/syscall.h>

#include <functional>
#include <iostream>

using namespace std::chrono_literals;

std::chrono::time_point<std::chrono::high_resolution_clock> Task::process_start;

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
    auto now = std::chrono::high_resolution_clock::now();
    std::cout << (now - Task::process_start).count() << ": starting task " << this->_id
              << " pid: " << this->_pid << std::endl;

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
    now = std::chrono::high_resolution_clock::now();
    std::cout << (now - Task::process_start).count() << ": stopping task " << this->_id
              << std::endl;
}

void Task::run_job() const {
    auto t_start = std::chrono::high_resolution_clock::now();
    auto t_end = t_start;
    std::chrono::duration<double> execution_time(this->_execution_time / 1000.0);
    std::cout << (t_start - Task::process_start).count() << ": starting job "  << this->_next_job
              << " for task " << this->_id << std::endl;
    for (int i = 0; i < this->_execution_time * 1000 * 400; ++i) {
        ;
    }
    t_end = std::chrono::high_resolution_clock::now();
    std::cout << (t_end - Task::process_start).count()
              << ": finished job " << this->_next_job << " for task " << this->_id
              << std::endl;
}

void Task::join() {
    this->_thread.join();
}
