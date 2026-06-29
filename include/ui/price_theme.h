#pragma once

#include <cstdint>

namespace ui::price {

constexpr int kSize     = 240;
constexpr int kCenterX  = kSize / 2;
constexpr int kCenterY  = kSize / 2;

constexpr int   kBarInnerR  = 72;
constexpr int   kBarMaxOuterR = 116;
constexpr int   kBarMaxH    = 44;
constexpr float kBarHalfWidth = 3.0f;

constexpr int   kTriBaseR   = 112;
constexpr int   kTriTipR    = 118;
constexpr float kTriHalfBase = 3.0f;

constexpr int kTextLargeY  = 107;
constexpr int kTextSmallY  = 136;

// RGB888 source colors (converted to RGB565 at init)
constexpr uint8_t kNegR = 0,   kNegG = 230, kNegB = 180;
constexpr uint8_t kLowR = 0,   kLowG = 180, kLowB = 60;
constexpr uint8_t kHighR = 220, kHighG = 40, kHighB = 30;
constexpr uint8_t kMidR = 100, kMidG = 100, kMidB = 100;
constexpr uint8_t kNaR  = 45,  kNaG  = 45,  kNaB  = 45;

}  // namespace ui::price
