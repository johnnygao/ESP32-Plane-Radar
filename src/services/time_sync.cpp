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

void todayBoundsUtc(time_t* start, time_t* end) {
  time_t now = time(nullptr);
  struct tm local_t;
  localtime_r(&now, &local_t);

  local_t.tm_hour  = 0;
  local_t.tm_min   = 0;
  local_t.tm_sec   = 0;
  local_t.tm_isdst = -1;
  *start = mktime(&local_t);

  // Next local midnight; days are 23 or 25 h on DST changes
  local_t.tm_mday += 1;
  local_t.tm_isdst = -1;
  *end = mktime(&local_t);
}

void buildPeriodStrings(char* from_buf, char* to_buf, size_t len) {
  time_t start = 0, end = 0;
  todayBoundsUtc(&start, &end);

  struct tm utc_t;
  gmtime_r(&start, &utc_t);
  strftime(from_buf, len, "%Y-%m-%dT%H:%M:%SZ", &utc_t);
  gmtime_r(&end, &utc_t);
  strftime(to_buf, len, "%Y-%m-%dT%H:%M:%SZ", &utc_t);
}
