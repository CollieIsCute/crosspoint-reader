#pragma once

#include <HalStorage.h>

#include <cstddef>

// Captures only UI profiling lines into a small buffered SD file. Normal writes
// stay buffered; end() flushes the remainder.
class UiProfileLog final {
 public:
  static constexpr const char* PATH = "/x4-ui-profile.log";

  UiProfileLog() = default;
  UiProfileLog(const UiProfileLog&) = delete;
  UiProfileLog& operator=(const UiProfileLog&) = delete;
  ~UiProfileLog();

  bool begin();
  void end();
  bool isActive() const { return active_; }

 private:
  static constexpr size_t BUFFER_SIZE = 4096;

  HalFile file_;
  char buffer_[BUFFER_SIZE] = {};
  size_t buffered_ = 0;
  bool active_ = false;

  static void receiveLine(const char* line, void* context);
  void appendLine(const char* line);
  void flush();
};
