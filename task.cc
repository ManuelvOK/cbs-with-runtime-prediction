#include "task.h"

time_point thread_now() {
    using rep = typename std::chrono::nanoseconds::rep;
    struct timespec cputime;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cputime);
    rep ns = static_cast<rep>(cputime.tv_nsec) +
        static_cast<rep>(cputime.tv_sec) * 1'000'000'000;
    return time_point(duration(ns));
}
