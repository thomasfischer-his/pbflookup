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

#include "error.h"

// DEPRECATED
#include <cstdio>
#include <cstring>

#include <unistd.h>

#include <fstream>

/// For std::exit
#include <cstdlib>

#include "config.h"
#include "global.h"

std::ofstream logfile; ///< declared in 'config.h'
LoggingLevel minimumLoggingLevel = LevelDebug;

Error::Error()
{
    /// nothing
}

/// Use colors only in terminal
const bool Error::useColor = isatty(1);

/// Prints a formatted message to stdout, optionally color coded
void Error::msg(MessageType messageType, const char *format, int color, va_list args) {
    static char message[maxStringLen];
    vsnprintf(message, maxStringLen - 1, format, args);

    /// Skip messages where level is lower than minimum logging level
    if ((int)messageType >= (int)minimumLoggingLevel) {
        if (useColor) {
            fprintf(stderr, "\x1b[0;%dm", color);
        }
        fputs(message, stderr);
        if (useColor) {
            fprintf(stderr, "\x1b[0m\n");
        } else {
            fprintf(stderr, "\n");
        }
    }

    /// Still log all messages to log file irrespective of logging level
    if (logfile.good()) {
        switch (messageType) {
        case MessageError: logfile << "ERR: "; break;
        case MessageWarn: logfile << "WRN: "; break;
        case MessageInfo: logfile << "INF: "; break;
        case MessageDebug: logfile << "DBG: "; break;
        }
        logfile << message << std::endl;
        logfile.flush();
    }
}

/// Prints a formatted message to stderr, color coded to red
void Error::err(const char *format, ...) {
    va_list args;
    va_start(args, format);
    msg(MessageError, format, 31, args);
    va_end(args);
    std::exit(1);
}

/// Prints a formatted message to stderr, color coded to yellow
void Error::warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    msg(MessageWarn, format, 33, args);
    va_end(args);
}

/// Prints a formatted message to stderr, color coded to green
void Error::info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    msg(MessageInfo, format, 32, args);
    va_end(args);
}

/// Prints a formatted message to stderr, color coded to white
void Error::debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    msg(MessageDebug, format, 37, args);
    va_end(args);
}


