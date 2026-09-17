#pragma once

#include <cstdio>
#include <cstdarg>
#include <string>
#include <mutex>

namespace vkwind {

enum class LogLevel {
  Off = 0,
  Error,
  Warn,
  Info,
  Debug,
  Trace,
};

class Logger {
public:
  static Logger& instance();

  void set_log_level(LogLevel level);
  void set_log_file(const char* path);

  void log(LogLevel level, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

  void vlog(LogLevel level, const char* tag, const char* fmt, va_list args);

private:
  Logger();
  ~Logger();

  LogLevel m_logLevel = LogLevel::Info;
  FILE* m_logFile = nullptr;
  std::mutex m_mutex;
};

#define VKWIND_LOG(level, tag, ...) \
  vkwind::Logger::instance().log(level, tag, __VA_ARGS__)

#define VKWIND_ERR(tag, ...)  VKWIND_LOG(vkwind::LogLevel::Error, tag, __VA_ARGS__)
#define VKWIND_WARN(tag, ...) VKWIND_LOG(vkwind::LogLevel::Warn,  tag, __VA_ARGS__)
#define VKWIND_INFO(tag, ...) VKWIND_LOG(vkwind::LogLevel::Info,  tag, __VA_ARGS__)
#define VKWIND_DBG(tag, ...)  VKWIND_LOG(vkwind::LogLevel::Debug, tag, __VA_ARGS__)
#define VKWIND_TRACE(tag, ...) VKWIND_LOG(vkwind::LogLevel::Trace, tag, __VA_ARGS__)

} // namespace vkwind
