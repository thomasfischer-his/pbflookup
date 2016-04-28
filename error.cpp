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

#include "error.h"

// DEPRECATED
#include <cstdio>
#include <cstring>

/// For std::exit
#include <cstdlib>

#include "config.h"

#define MAX_STRING_LEN 1024

FILE *logfile = NULL; ///< declared in 'config.h'
LoggingLevel minimumLoggingLevel = LevelDebug;

Error::Error()
{
    /// nothing
}

bool Error::useColor = true;

/// Prints a formatted message to stdout, optionally color coded
void Error::msg(MessageType messageType, const char *format, int color, va_list args) {
    /// Skip messages where level is lower than minimum logging level
    if ((int)messageType < (int)minimumLoggingLevel) return;

    static char message[MAX_STRING_LEN];

    vsnprintf(message, MAX_STRING_LEN, format, args);

    if (useColor) {
        fprintf(stderr, "\x1b[0;%dm", color);
    }
    fputs(message, stderr);
    if (useColor) {
        fprintf(stderr, "\x1b[0m\n");
    } else {
        fprintf(stderr, "\n");
    }

    if (logfile != NULL) {
        switch (messageType) {
        case MessageError: fputs("ERR: ", logfile); break;
        case MessageWarn: fputs("WRN: ", logfile); break;
        case MessageInfo: fputs("INF: ", logfile); break;
        case MessageDebug: fputs("DBG: ", logfile); break;
        }
        fputs(message, logfile);
        fputs("\n", logfile);
        fflush(logfile);
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


