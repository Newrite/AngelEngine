module;

#include <cstdint>
#include <format>
#include <print>
#include <utility>
#include <string>

export module AngelEngine.Logger;

namespace AngelEngine
{
    export enum class LogLevel : uint8_t
    {
        Info,
        Warning,
        Error
    };

    export struct ILogger
    {
        virtual ~ILogger() = default;
        virtual void Log(LogLevel level, const char* message) = 0;
    };

    export class Log
    {
    public:
        static inline void SetLogger(ILogger* logger)
        {
            g_logger = logger;
        }

        template<typename... Args>
        static void Info(std::format_string<Args...> fmt, Args&&... args)
        {
            if (g_logger)
            {
                std::string msg = std::format(fmt, std::forward<Args>(args)...);
                g_logger->Log(LogLevel::Info, msg.c_str());
            }
            else
            {
                std::println(stdout, "{}", std::format(fmt, std::forward<Args>(args)...));
            }
        }

        template<typename... Args>
        static void Warning(std::format_string<Args...> fmt, Args&&... args)
        {
            if (g_logger)
            {
                std::string msg = std::format(fmt, std::forward<Args>(args)...);
                g_logger->Log(LogLevel::Warning, msg.c_str());
            }
            else
            {
                std::println(stdout, "[WARN] {}", std::format(fmt, std::forward<Args>(args)...));
            }
        }

        template<typename... Args>
        static void Error(std::format_string<Args...> fmt, Args&&... args)
        {
            if (g_logger)
            {
                std::string msg = std::format(fmt, std::forward<Args>(args)...);
                g_logger->Log(LogLevel::Error, msg.c_str());
            }
            else
            {
                std::println(stderr, "[ERROR] {}", std::format(fmt, std::forward<Args>(args)...));
            }
        }

    private:
        static inline ILogger* g_logger = nullptr;
    };
}