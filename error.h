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

#ifndef ERROR_H
#define ERROR_H

/// Used for va_list in debug-print methods
#include <cstdarg>

class Error
{
public:
    static bool useColor;

    /// Prints a formatted message to stderr, color coded to red
    static void err(const char *format, ...);
    /// Prints a formatted message to stderr, color coded to yellow
    static void warn(const char *format, ...);
    /// Prints a formatted message to stderr, color coded to green
    static void info(const char *format, ...);
    /// Prints a formatted message to stderr, color coded to white
    static void debug(const char *format, ...);

private:
    enum MessageType {MessageError, MessageWarn, MessageInfo, MessageDebug};

    Error();

    static void msg(MessageType messageType, const char *format, int color, va_list args);
};

#endif // ERROR_H
