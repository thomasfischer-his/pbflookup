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
    Error();

    static void msg(const char *format, int color, va_list args);
};

#endif // ERROR_H
