#include "util_log.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <chrono>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#endif

namespace vkwind {

static const char* log_level_str(LogLevel level) {
  switch (level) {
    case LogLevel::Off:   return "OFF";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Trace: return "TRACE";
    default:              return "?????";
  }
}

Logger& Logger::instance() {
  static Logger s_instance;
  return s_instance;
}

Logger::Logger() {
#ifdef _WIN32
  // Debug output on Windows
  if (IsDebuggerPresent()) {
    m_logLevel = LogLevel::Debug;
  }
#endif
}

Logger::~Logger() {
  if (m_logFile) {
    fclose(m_logFile);
    m_logFile = nullptr;
  }
}

void Logger::set_log_level(LogLevel level) {
  m_logLevel = level;
}

void Logger::set_log_file(const char* path) {
  std::lock_guard lock(m_mutex);
  if (m_logFile) {
    fclose(m_logFile);
  }
  m_logFile = fopen(path, "w");
}

void Logger::log(LogLevel level, const char* tag, const char* fmt, ...) {
  if (level > m_logLevel) return;
  va_list args;
  va_start(args, fmt);
  vlog(level, tag, fmt, args);
  va_end(args);
}

void Logger::vlog(LogLevel level, const char* tag, const char* fmt, va_list args) {
  if (level > m_logLevel) return;

  std::lock_guard lock(m_mutex);

  // Timestamp
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    now.time_since_epoch()) % 1000;

  char timebuf[32];
  struct tm tm_buf;
#ifdef _WIN32
  localtime_s(&tm_buf, &time);
#else
  localtime_r(&time, &tm_buf);
#endif
  snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d.%03d",
    tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, (int)ms.count());

  char msgbuf[4096];
  vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);

  char linebuf[4200];
  snprintf(linebuf, sizeof(linebuf), "[%s] [%s] [%s] %s\n",
    timebuf, log_level_str(level), tag, msgbuf);

  // Console output
  fprintf(stdout, "%s", linebuf);
  fflush(stdout);

#ifdef _WIN32
  // Also send to debug output
  OutputDebugStringA(linebuf);
#endif

  // File output
  if (m_logFile) {
    fprintf(m_logFile, "%s", linebuf);
    fflush(m_logFile);
  }
}

} // namespace vkwind
