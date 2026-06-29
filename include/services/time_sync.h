#pragma once

#include <cstddef>
#include <cstdint>

void timeSyncInit();
bool timeSyncWait(uint32_t ms);
int  currentSlotIndex();
int  minuteOfDay();
void buildPeriodStrings(char* from_buf, char* to_buf, size_t len);
