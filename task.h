#pragma once

#include <chrono>
#include <semaphore>
#include <thread>

class Task {
  public:

    static std::chrono::time_point<std::chrono::high_resolution_clock> process_start;

    int _id;
    std::counting_semaphore<> _sem;
    int _execution_time;
    int _period;
    int _n_jobs;

    std::thread _thread;
    bool _running = true;
    int _pid = 0;
    float _next_period = 0;
    int _next_job = 0;
    int _n_jobs_waiting = 0;

    Task(int id, int execution_time, int period, int n_jobs);

    void run_task();

    void run_job() const;

    void join();
};
