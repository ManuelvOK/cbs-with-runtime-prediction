#pragma once

#include <chrono>
#include <fstream>
#include <semaphore>
#include <thread>
#include <queue>

#include <predictor/predictor.h>

using time_point = std::chrono::time_point<std::chrono::steady_clock>;
using duration = typename std::chrono::nanoseconds;

struct Job {
    int _id;
    duration _execution_time;
    time_point _deadline;
    time_point _submission_time;
    int _task_id;
};

class Task {
  public:
    static bool _prediction_enabled;
    int _id;
    std::counting_semaphore<> _sem;
    duration _execution_time;
    duration _period;

    std::vector<double> _runtimes;
    atlas::estimator _predictor;

    std::thread _thread;
    bool _running = true;
    int _pid = 0;
    double _result = 1.5;

    std::queue<struct Job> _jobs;

    Task(int id, duration execution_time, duration period);

    void run_task();

    void run_job();

    void join();
};

