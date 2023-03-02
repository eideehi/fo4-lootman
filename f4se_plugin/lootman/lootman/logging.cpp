#include "logging.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

#include "f4se/GameTypes.h"

namespace logging
{
    SimpleLock logLock;

    std::string getCurrentTime()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    void Fatal(const char * fmt, ...)
    {
        SimpleLocker locker(&logLock);
        va_list args;

        std::string new_fmt = getCurrentTime() + " " + std::string(fmt);

        va_start(args, fmt);
        gLog.Log(IDebugLog::kLevel_FatalError, new_fmt.c_str(), args);
        va_end(args);
    }

    void Error(const char * fmt, ...)
    {
        SimpleLocker locker(&logLock);
        va_list args;

        std::string new_fmt = getCurrentTime() + " " + std::string(fmt);

        va_start(args, fmt);
        gLog.Log(IDebugLog::kLevel_Error, new_fmt.c_str(), args);
        va_end(args);
    }

    void Warning(const char * fmt, ...)
    {
        SimpleLocker locker(&logLock);
        va_list args;

        std::string new_fmt = getCurrentTime() + " " + std::string(fmt);

        va_start(args, fmt);
        gLog.Log(IDebugLog::kLevel_Warning, new_fmt.c_str(), args);
        va_end(args);
    }

    void Message(const char * fmt, ...)
    {
        SimpleLocker locker(&logLock);
        va_list args;

        std::string new_fmt = getCurrentTime() + " " + std::string(fmt);

        va_start(args, fmt);
        gLog.Log(IDebugLog::kLevel_Message, new_fmt.c_str(), args);
        va_end(args);
    }

    void Verbose(const char * fmt, ...)
    {
        SimpleLocker locker(&logLock);
        va_list args;

        std::string new_fmt = getCurrentTime() + " " + std::string(fmt);

        va_start(args, fmt);
        gLog.Log(IDebugLog::kLevel_VerboseMessage, new_fmt.c_str(), args);
        va_end(args);
    }

    void Debug(const char * fmt, ...)
    {
        SimpleLocker locker(&logLock);
        va_list args;

        std::string new_fmt = getCurrentTime() + " " + std::string(fmt);

        va_start(args, fmt);
        gLog.Log(IDebugLog::kLevel_DebugMessage, new_fmt.c_str(), args);
        va_end(args);
    }
}
