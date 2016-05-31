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
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#ifndef TIMER_H
#define TIMER_H

#include <cstdint>
#include <ctime>

class Timer
{
public:
    Timer();

    void start();
    void stop();

    void elapsed(int64_t *elapsed_cpu, int64_t *elapsed_wall = nullptr);

private:
    struct timespec previous_cpu;
    struct timeval previous_wall;

    int64_t stopped_elapsed_cpu, stopped_elapsed_wall;
};

#endif // TIMER_H
