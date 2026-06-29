#include "ui/price_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/octopus_client.h"
#include "services/time_sync.h"
#include "ui/price_theme.h"

namespace price = ui::price;

namespace {

lgfx::LovyanGFX* s_draw   = &tft;
LGFX_Sprite      s_frame(&tft);
bool             s_frame_ready = false;
int              s_last_slot_index = -1;

// Palette (RGB565, computed once at init)
uint16_t s_col_neg;
uint16_t s_col_low;
uint16_t s_col_high;
uint16_t s_col_mid;
uint16_t s_col_na;
uint16_t s_col_bg;
uint16_t s_col_white;

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }
 private:
  lgfx::LovyanGFX* prev_;
};

uint16_t makeColor(uint8_t r, uint8_t g, uint8_t b) {
  // GC9A01 BGR panel: swap R/B so logical colours render correctly.
  return config::kDisplayRgbOrder ? tft.color565(b, g, r) : tft.color565(r, g, b);
}

bool ensureFrameSprite() {
  if (s_frame_ready) {
    return true;
  }
  s_frame.setColorDepth(16);
  if (!s_frame.createSprite(price::kSize, price::kSize)) {
    Serial.println("price: frame sprite alloc failed");
    return false;
  }
  s_frame_ready = true;
  return true;
}

uint16_t slotColor(float price, float p20, float p80, bool available) {
  if (!available) {
    return s_col_na;
  }
  if (price < 0.0f) {
    return s_col_neg;
  }
  if (price <= p20) {
    return s_col_low;
  }
  if (price > p80) {
    return s_col_high;
  }
  return s_col_mid;
}

void computePercentiles(const services::octopus::PriceSlot* slots,
                        float* out_p20, float* out_p80) {
  float prices[services::octopus::kSlotCount];
  int n = 0;
  for (int i = 0; i < services::octopus::kSlotCount; ++i) {
    if (slots[i].available) {
      prices[n++] = slots[i].value_inc_vat;
    }
  }
  if (n == 0) {
    *out_p20 = 0.0f;
    *out_p80 = 100.0f;
    return;
  }
  std::sort(prices, prices + n);
  *out_p20 = prices[(n * 20) / 100];
  *out_p80 = prices[(n * 80) / 100];
}

void drawBars() {
  const services::octopus::PriceSlot* slots = services::octopus::priceSlots();
  const int current_slot = currentSlotIndex();

  float p20 = 0.0f, p80 = 0.0f;
  computePercentiles(slots, &p20, &p80);

  // Day range for bar height scaling
  float actual_min = FLT_MAX, actual_max = -FLT_MAX;
  for (int i = 0; i < services::octopus::kSlotCount; ++i) {
    if (!slots[i].available) continue;
    const float v = slots[i].value_inc_vat;
    if (v < actual_min) actual_min = v;
    if (v > actual_max) actual_max = v;
  }
  if (actual_min == FLT_MAX) { actual_min = 0.0f; actual_max = 1.0f; }
  const float day_min    = fmaxf(actual_min, -10.0f);
  const float day_max    = actual_max;
  const float price_range = fmaxf(day_max - day_min, 1.0f);

  for (int i = 0; i < services::octopus::kSlotCount; ++i) {
    const float angle_rad =
        (-90.0f + static_cast<float>(i) * 7.5f) * static_cast<float>(M_PI) / 180.0f;
    const float cos_a = cosf(angle_rad);
    const float sin_a = sinf(angle_rad);

    const bool avail = slots[i].available;
    const float price = slots[i].value_inc_vat;
    float h_px;
    if (!avail) {
      h_px = 2.0f;
    } else {
      h_px = (price - day_min) / price_range * static_cast<float>(price::kBarMaxH);
      h_px = fmaxf(2.0f, fminf(static_cast<float>(price::kBarMaxH), h_px));
    }

    const float r0 = static_cast<float>(price::kBarInnerR);
    const float r1 = r0 + h_px;
    const float cx = static_cast<float>(price::kCenterX);
    const float cy = static_cast<float>(price::kCenterY);

    const float x0 = cx + r0 * cos_a;
    const float y0 = cy + r0 * sin_a;
    const float x1 = cx + r1 * cos_a;
    const float y1 = cy + r1 * sin_a;

    const uint16_t bar_color = slotColor(price, p20, p80, avail);

    // Current slot: white outline (wider line) then colored bar on top
    if (i == current_slot) {
      s_draw->drawWideLine(x0, y0, x1, y1, price::kBarHalfWidth + 1.0f, s_col_white);
    }
    s_draw->drawWideLine(x0, y0, x1, y1, price::kBarHalfWidth, bar_color);
  }
}

void drawTriangle(int slot_i) {
  const float angle_rad =
      (-90.0f + static_cast<float>(slot_i) * 7.5f) * static_cast<float>(M_PI) / 180.0f;
  const float cos_a = cosf(angle_rad);
  const float sin_a = sinf(angle_rad);
  const float cx = static_cast<float>(price::kCenterX);
  const float cy = static_cast<float>(price::kCenterY);

  const int tip_x = static_cast<int>(cx + price::kTriBaseR * cos_a + 0.5f);
  const int tip_y = static_cast<int>(cy + price::kTriBaseR * sin_a + 0.5f);

  const float base_cx = cx + price::kTriTipR * cos_a;
  const float base_cy = cy + price::kTriTipR * sin_a;

  const int bl_x = static_cast<int>(base_cx - price::kTriHalfBase * sin_a + 0.5f);
  const int bl_y = static_cast<int>(base_cy + price::kTriHalfBase * cos_a + 0.5f);
  const int br_x = static_cast<int>(base_cx + price::kTriHalfBase * sin_a + 0.5f);
  const int br_y = static_cast<int>(base_cy - price::kTriHalfBase * cos_a + 0.5f);

  s_draw->fillTriangle(tip_x, tip_y, bl_x, bl_y, br_x, br_y, s_col_white);
}

void drawCenterText() {
  const int slot_i = currentSlotIndex();
  const services::octopus::PriceSlot& slot = services::octopus::priceSlots()[slot_i];

  char price_str[12];
  if (slot.available) {
    snprintf(price_str, sizeof(price_str), "%.1fp", slot.value_inc_vat);
  } else {
    snprintf(price_str, sizeof(price_str), "--p");
  }

  const int slot_hour = (slot_i * 30) / 60;
  const int slot_min  = (slot_i * 30) % 60;
  char time_str[8];
  snprintf(time_str, sizeof(time_str), "%02d:%02d", slot_hour, slot_min);

  s_draw->setTextDatum(textdatum_t::middle_center);
  s_draw->setTextColor(s_col_white, s_col_bg);

  displayFontSetBitmap(*s_draw, &fonts::FreeMonoBold18pt7b);
  s_draw->drawString(price_str, price::kCenterX, price::kTextLargeY);
  displayFontSetBitmap(*s_draw, &fonts::FreeMonoBold12pt7b);
  s_draw->drawString(time_str,  price::kCenterX, price::kTextSmallY);
}

void renderFrame() {
  const DrawScope scope(s_frame);

  s_draw->fillScreen(s_col_bg);
  drawBars();
  drawTriangle(currentSlotIndex());
  drawCenterText();

  s_frame.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void priceDisplayInit() {
  s_col_neg   = makeColor(price::kNegR,  price::kNegG,  price::kNegB);
  s_col_low   = makeColor(price::kLowR,  price::kLowG,  price::kLowB);
  s_col_high  = makeColor(price::kHighR, price::kHighG, price::kHighB);
  s_col_mid   = makeColor(price::kMidR,  price::kMidG,  price::kMidB);
  s_col_na    = makeColor(price::kNaR,   price::kNaG,   price::kNaB);
  s_col_bg    = 0x0000;
  s_col_white = 0xFFFF;

  ensureFrameSprite();
}

void priceDisplayDraw() {
  if (ensureFrameSprite()) {
    renderFrame();
    s_last_slot_index = currentSlotIndex();
    return;
  }
  // Fallback: draw direct to panel without double-buffer
  const DrawScope scope(tft);
  displayFontEnsureLoaded(tft);
  s_draw->fillScreen(s_col_bg);
  drawBars();
  drawTriangle(currentSlotIndex());
  drawCenterText();
  tft.setTextDatum(textdatum_t::top_left);
  s_last_slot_index = currentSlotIndex();
}

void priceDisplayUpdate() {
  const int current = currentSlotIndex();
  if (current == s_last_slot_index) {
    return;
  }
  priceDisplayDraw();
}
