#include "error.h"

// DEPRECATED
#include <cstdio>

/// For std::exit
#include <cstdlib>


Error::Error()
{
    /// nothing
}

bool Error::useColor = true;

/// Prints a formatted message to stdout, optionally color coded
void Error::msg(const char *format, int color, va_list args) {
    if (useColor) {
        fprintf(stderr, "\x1b[0;%dm", color);
    }
    vfprintf(stderr, format, args);
    if (useColor) {
        fprintf(stderr, "\x1b[0m\n");
    } else {
        fprintf(stderr, "\n");
    }
}

/// Prints a formatted message to stderr, color coded to red
void Error::err(const char *format, ...) {
    va_list args;
    va_start(args, format);
    msg(format, 31, args);
    va_end(args);
    std::exit(1);
}

/// Prints a formatted message to stderr, color coded to yellow
void Error::warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    msg(format, 33, args);
    va_end(args);
}

/// Prints a formatted message to stderr, color coded to green
void Error::info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    msg(format, 32, args);
    va_end(args);
}

/// Prints a formatted message to stderr, color coded to white
void Error::debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    msg(format, 37, args);
    va_end(args);
}


