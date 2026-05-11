#pragma once

#include <string>
#include <source_location>

namespace chimera::utils {

enum class LogLevel { Debug, Info, Warning, Error, Fatal };

class Logger {
public:
    static Logger &instance();

    void setLogLevel(LogLevel level);
    void setOutputFile(const std::string &path);

    void log(LogLevel level, const std::string &msg, const std::source_location &loc = std::source_location::current());

    void debug(const std::string &msg, const std::source_location &loc = std::source_location::current()) { log(LogLevel::Debug, msg, loc); }
    void info(const std::string &msg, const std::source_location &loc = std::source_location::current()) { log(LogLevel::Info, msg, loc); }
    void warning(const std::string &msg, const std::source_location &loc = std::source_location::current()) { log(LogLevel::Warning, msg, loc); }
    void error(const std::string &msg, const std::source_location &loc = std::source_location::current()) { log(LogLevel::Error, msg, loc); }
    void fatal(const std::string &msg, const std::source_location &loc = std::source_location::current()) { log(LogLevel::Fatal, msg, loc); }

private:
    Logger() = default;
    LogLevel m_level = LogLevel::Info;
    std::string m_filePath;
};

} // namespace chimera::utils
