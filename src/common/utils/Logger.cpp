#include "Logger.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>

namespace chimera::utils {

static std::mutex g_logMutex;

Logger &Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::setLogLevel(LogLevel level) {
    m_level = level;
}

void Logger::setOutputFile(const std::string &path) {
    m_filePath = path;
}

void Logger::log(LogLevel level, const std::string &msg, const std::source_location &loc) {
    if (level < m_level) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);
    const char *lvlStr = "?";
    switch (level) {
        case LogLevel::Debug: lvlStr = "D"; break;
        case LogLevel::Info: lvlStr = "I"; break;
        case LogLevel::Warning: lvlStr = "W"; break;
        case LogLevel::Error: lvlStr = "E"; break;
        case LogLevel::Fatal: lvlStr = "F"; break;
    }
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " [" << lvlStr << "] "
        << loc.file_name() << ":" << loc.line() << " " << msg;
    std::string line = oss.str();
    std::cout << line << std::endl;
    if (!m_filePath.empty()) {
        std::ofstream f(m_filePath, std::ios::app);
        if (f.is_open()) f << line << "\n";
    }
}

} // namespace chimera::utils
