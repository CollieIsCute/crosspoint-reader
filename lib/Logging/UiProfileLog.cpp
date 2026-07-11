#include "UiProfileLog.h"

#include <Logging.h>

#include <cstring>

namespace {
constexpr char MODULE[] = "FBPROF";
constexpr size_t MAX_CAPTURE_LINE = 512;

bool isUiProfileLine(const char* line) {
  return strstr(line, "[FBPROF]") != nullptr || strstr(line, "[SDCF] [FileBrowser]") != nullptr;
}
}  // namespace

UiProfileLog::~UiProfileLog() { end(); }

bool UiProfileLog::begin() {
  end();
  if (!Storage.openFileForWrite(MODULE, PATH, file_)) return false;

  buffered_ = 0;
  active_ = true;
  setLogSink(&UiProfileLog::receiveLine, this);
  return true;
}

void UiProfileLog::end() {
  setLogSink(nullptr, nullptr);
  if (!active_) return;

  flush();
  file_.flush();
  file_.close();
  active_ = false;
}

void UiProfileLog::receiveLine(const char* line, void* context) {
  if (line == nullptr || context == nullptr) return;
  static_cast<UiProfileLog*>(context)->appendLine(line);
}

void UiProfileLog::appendLine(const char* line) {
  if (!active_ || line == nullptr || !isUiProfileLine(line)) return;

  const size_t length = strnlen(line, MAX_CAPTURE_LINE);
  if (length == 0) return;

  if (buffered_ + length > sizeof(buffer_)) {
    flush();
  }

  if (length > sizeof(buffer_)) {
    file_.write(line, length);
    return;
  }

  memcpy(buffer_ + buffered_, line, length);
  buffered_ += length;
}

void UiProfileLog::flush() {
  if (!active_ || buffered_ == 0) return;
  file_.write(buffer_, buffered_);
  buffered_ = 0;
}
