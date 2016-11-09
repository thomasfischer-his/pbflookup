/***************************************************************************
 *   Copyright (C) 2015-2016 by Thomas Fischer <thomas.fischer@his.se>     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; version 3 of the License.               *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "timer.h"

#include <boost/date_time/posix_time/posix_time.hpp>

/** Subtract the `struct timeval' values X and Y, storing the result in RESULT.
 * Return 1 if the difference is negative, otherwise 0.
 */
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
       tv_usec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}

Timer::Timer() {
    start();
}

void Timer::start() {
    stopped_elapsed_cpu = stopped_elapsed_wall = -1;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &previous_cpu);
    gettimeofday(&previous_wall, NULL);
}

void Timer::stop() {
    elapsed(&stopped_elapsed_cpu, &stopped_elapsed_wall);
}

void Timer::elapsed(int64_t *elapsed_cpu, int64_t *elapsed_wall) {
    if (stopped_elapsed_cpu >= 0 && stopped_elapsed_wall >= 0) {
        /// Timer was previously stopped with stop()
        if (elapsed_cpu != nullptr) *elapsed_cpu = stopped_elapsed_cpu;
        if (elapsed_wall != nullptr) *elapsed_wall = stopped_elapsed_wall;
        return;
    }

    if (elapsed_cpu != nullptr) {
        struct timespec now_cpu;
        if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now_cpu) == 0) {
            const int64_t now_us = (int64_t)now_cpu.tv_sec * 1000000 + now_cpu.tv_nsec / 1000;
            const int64_t previous_us = (int64_t)previous_cpu.tv_sec * 1000000 + previous_cpu.tv_nsec / 1000;
            *elapsed_cpu = now_us - previous_us;
        } else
            *elapsed_cpu = -1;
    }

    if (elapsed_wall != nullptr) {
        struct timeval now_wall, result_wall;
        if (gettimeofday(&now_wall, NULL) == 0 && timeval_subtract(&result_wall, &now_wall, &previous_wall) == 0) {
            *elapsed_wall = (int64_t)result_wall.tv_sec * 1000000 + result_wall.tv_usec;
        } else
            *elapsed_wall = -1;
    }
}
