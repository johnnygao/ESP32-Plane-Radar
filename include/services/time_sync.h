#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

void timeSyncInit();
bool timeSyncWait(uint32_t ms);
int  currentSlotIndex();
int  minuteOfDay();
/** UTC epochs of today's UK local midnight and the next one. */
void todayBoundsUtc(time_t* start, time_t* end);
void buildPeriodStrings(char* from_buf, char* to_buf, size_t len);
