#pragma once

#include <chrono>
#include <fstream>
#include <semaphore>
#include <thread>
#include <queue>

using time_point = std::chrono::time_point<std::chrono::steady_clock>;
using duration = typename std::chrono::nanoseconds;

struct Job {
    int _id;
    duration _execution_time;
    time_point _deadline;
};

class Task {
  public:

    static time_point process_start;

    int _id;
    std::counting_semaphore<> _sem;
    duration _execution_time;
    duration _period;

    std::thread _thread;
    bool _running = true;
    int _pid = 0;
    time_point _next_period;
    int _next_job = 0;
    int _n_jobs_waiting = 0;
    double _result = 1.5;

    std::vector<std::string> _events;
    std::queue<struct Job> _jobs;

    Task(int id, duration execution_time, duration period);

    void run_task();

    void run_job();

    void join();

    void write_back_events();
};

