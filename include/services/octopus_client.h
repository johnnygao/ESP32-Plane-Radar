#pragma once

#include <cstddef>

namespace services::octopus {

struct PriceSlot {
  float value_inc_vat;
  bool  available;
};

constexpr int kSlotCount = 48;

using PollFn = void (*)();
void setPollFn(PollFn fn);

bool discoverProductCode();
const char* productCode();

bool fetchTodayPrices(char region);
const PriceSlot* priceSlots();
float currentSlotPrice();

}  // namespace services::octopus
