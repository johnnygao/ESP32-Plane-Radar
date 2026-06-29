#include "services/time_sync.h"

#include <Arduino.h>
#include <time.h>

#include "config.h"

// UK: GMT standard, BST (UTC+1) last Sunday March 01:00 → last Sunday October 01:00
static constexpr char kTzUk[] = "GMT0BST,M3.5.0/1,M10.5.0";

void timeSyncInit() {
  configTime(0, 0, config::kNtpServer1, config::kNtpServer2);
  setenv("TZ", kTzUk, 1);
  tzset();
}

bool timeSyncWait(uint32_t ms) {
  const uint32_t deadline = millis() + ms;
  while (millis() < deadline) {
    if (time(nullptr) > 1000000000UL) {
      return true;
    }
    delay(100);
  }
  return time(nullptr) > 1000000000UL;
}

int currentSlotIndex() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return (t.tm_hour * 60 + t.tm_min) / 30;
}

int minuteOfDay() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return t.tm_hour * 60 + t.tm_min;
}

void buildPeriodStrings(char* from_buf, char* to_buf, size_t len) {
  time_t now = time(nullptr);
  struct tm local_t;
  localtime_r(&now, &local_t);

  // Truncate to midnight of the current UK calendar day
  local_t.tm_hour  = 0;
  local_t.tm_min   = 0;
  local_t.tm_sec   = 0;
  local_t.tm_isdst = -1;
  const time_t midnight = mktime(&local_t);

  struct tm utc_from;
  gmtime_r(&midnight, &utc_from);
  strftime(from_buf, len, "%Y-%m-%dT%H:%M:%SZ", &utc_from);

  const time_t tomorrow = midnight + 86400;
  struct tm utc_to;
  gmtime_r(&tomorrow, &utc_to);
  strftime(to_buf, len, "%Y-%m-%dT%H:%M:%SZ", &utc_to);
}
