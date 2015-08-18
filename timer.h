/***************************************************************************
 *   Copyright (C) 2015 by Thomas Fischer <thomas.fischer@his.se>          *
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
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

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
