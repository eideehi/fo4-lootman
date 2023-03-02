#pragma once

namespace logging
{
    void Fatal(const char * fmt, ...);
    void Error(const char * fmt, ...);
    void Warning(const char * fmt, ...);
    void Message(const char * fmt, ...);
    void Verbose(const char * fmt, ...);
    void Debug(const char * fmt, ...);
}
