#ifndef TIMER_H
#define TIMER_H

#include <ctime>

class Timer
{
public:
    Timer() {
        start();
    }

    void start() {
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &previous);
    }

    int64_t elapsed() {
        struct timespec now;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
        const int64_t now_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
        const int64_t previous_us = (int64_t)previous.tv_sec * 1000000 + previous.tv_nsec / 1000;
        return now_us - previous_us;
    }

private:
    struct timespec previous;
};

#endif // TIMER_H
