# Octopus Agile Price Display — Implementation Plan

Convert the ESP32-C3 Super Mini + 1.28" GC9A01 firmware from an ADS-B plane radar into an
Octopus Agile electricity price visualiser. Hardware is identical; only the firmware changes.
The existing codebase contributes: LovyanGFX sprite double-buffering, WiFiManager captive
portal, HTTPClient/WiFiClientSecure HTTPS pattern, ArduinoJson parsing, and boot-button flow.

---

## What Changes, What Stays

**Kept unchanged**
- `lgfx_config.hpp`, display hardware layer (`display.cpp`/`.h`, `display_font.*`)
- `status_screens` (connecting/error screens)
- `data/ui_font.vlw` (Noto Sans Bold — reused for center text)
- `DrawScope` pattern, `LGFX_Sprite` double-buffering
- Partition table, WiFiManager portal flow skeleton, build toolchain
- `scripts/merge_firmware.py` + `merge-firmware.sh` (release pipeline unchanged)

**Replaced (same slot, new content)**

| Old | New |
|---|---|
| `services/adsb_client` | `services/octopus_client` |
| `services/radar_location` | `services/time_sync` |
| `ui/radar_display` + `ui/radar_theme` | `ui/price_display` + `ui/price_theme` |

**Removed**
- `ui/runway_overlay`, `ui/radar_range`
- `data/large_airports*`, `scripts/build_large_airports.py`

**Modified**
- `include/config.h` — replace ADS-B/radar constants with Octopus constants
- `src/main.cpp` — new setup/loop flow
- `src/services/wifi_setup.cpp` + header — replace 4 portal params with 1 (region)

---

## Phase 1 — Foundation

### Timezone
Since Octopus Agile is UK-only, the timezone is always `Europe/London`. Hardcoded via POSIX
TZ string `"GMT0BST,M3.5.0/1,M10.5.0"` — no selector, no geolocation needed.

### New: `services/time_sync`
```cpp
void timeSyncInit();                 // configTime() + setenv TZ + tzset()
bool timeSyncWait(uint32_t ms);      // spin until NTP synced (epoch > 1e9)
int  currentSlotIndex();             // 0–47: (hour*60+min)/30 in UK local time
int  minuteOfDay();                  // hour*60+min in UK local time
void buildPeriodStrings(            // ISO-8601 UTC strings for today's UK calendar day
    char* from_buf, char* to_buf, size_t len);
```

`buildPeriodStrings` takes UK local midnight, converts to UTC — correctly handles BST so
the fetch window always covers the full UK calendar day regardless of DST offset.

### NVS
New namespace `"octopus"`:
- `"region"` — 1 char (A–P), DNO region letter
- `"product"` — ≤32 chars, cached Agile product code

### WiFi Portal
Remove existing 4 params (lat, lon, miles, runways). Add one:

**DNO Region** (`octopus_region`, default `"C"` London)  
Label: `"DNO Region: A=East B=EMidlands C=London D=Merseyside E=WMidlands F=NE G=NW H=South J=SE K=SWales L=SW M=Yorks N=SScotland P=NScotland"`  
Input: `maxlength="1" style="text-transform:uppercase"` — single char, uppercased.  
Save callback: validate A–P, persist to NVS, expose via `services::wifi::region()`.

### config.h
```cpp
constexpr char kOctopusApiBase[]         = "https://api.octopus.energy/v1";
constexpr char kOctopusFallbackProduct[] = "AGILE-FLEX-22-11-25";
constexpr char kNtpServer1[]             = "pool.ntp.org";
constexpr char kNtpServer2[]             = "time.nist.gov";
constexpr unsigned long kPriceFetchTimeoutMs = 15000;
```

---

## Phase 2 — Octopus Client Service

**New:** `include/services/octopus_client.h` / `src/services/octopus_client.cpp`

Reuse verbatim from `adsb_client.cpp`: `performGetWithPoll()`, `readResponseBodyWithPoll()`,
`WiFiClientSecure::setInsecure()`, `setPollFn()` hook mechanism (identical interface).

### Data structure
```cpp
struct PriceSlot {
  float value_inc_vat;  // p/kWh; may be negative
  bool  available;      // false = Octopus hasn't published this slot yet
};
constexpr int kSlotCount = 48;
```

### Product discovery
```
GET /v1/products/?is_variable=true&brand=OCTOPUS_ENERGY&page_size=100
```
Filter: `display_name == "Agile Octopus"`, `direction == "IMPORT"`, `is_prepay == false`.
Take first result (newest first). Cache to NVS `"octopus"/"product"`.

Fallback chain: cached NVS value → `kOctopusFallbackProduct`.

```cpp
bool discoverProductCode();  // fetch + cache; returns false only if both fail
const char* productCode();   // returns cached/fallback code
```

### Price fetch
```
GET /v1/products/{PRODUCT}/electricity-tariffs/E-1R-{PRODUCT}-{REGION}/standard-unit-rates/
    ?period_from={from}&period_to={to}&page_size=48
```
`from`/`to` from `timeSyncGetDateUTC()`. Response is `{ "results": [{ "value_inc_vat",
"valid_from", "valid_to" }] }`. Map each result to a slot index: parse `valid_from` UTC
timestamp → convert to UK local time → `(hour*60+min)/30`.

```cpp
bool fetchTodayPrices(char region);  // populates PriceSlot[48]; retains stale data on failure
const PriceSlot* priceSlots();
float currentSlotPrice();            // priceSlots()[currentSlotIndex()].value_inc_vat
```

---

## Phase 3 — Price Display

**New:** `include/ui/price_display.h` / `src/ui/price_display.cpp` / `include/ui/price_theme.h`

Same `LGFX_Sprite` double-buffer + `DrawScope` pattern as `radar_display.cpp`.

### Geometry (240×240 display, center at 120,120)

| Element | Value |
|---|---|
| Base circle radius (inner bar end) | 72 px |
| Max bar outer radius | 116 px (4 px edge margin) |
| Max bar height | 44 px |
| Center text area | ≤68 px from center |
| Slot 0 (00:00) | top, 12 o'clock (−90° math) |
| Angle step | 7.5° clockwise |
| `drawWideLine` half-width | 3 px → ~6 px bar, ~3 px gap |

`angle_rad[i] = (-90.0 + i * 7.5) * PI / 180.0`

### Color scheme (RGB888 → RGB565 at init, percentile-based per fetch)

| Condition | Color |
|---|---|
| `price < 0` | Bright cyan `(0, 230, 180)` |
| `≤ 20th percentile` | Green `(0, 180, 60)` |
| `> 80th percentile` | Red `(220, 40, 30)` |
| Middle 60% | Grey `(100, 100, 100)` |
| Not yet available | Dark grey `(45, 45, 45)`, 2 px stub |

Current slot: same color + white `drawWideLine` outline at `r+1`.

### Bar rendering
```
for i in 0..47:
  a = (-90 + i*7.5) * PI/180
  h = available ? clamp((price - day_min) / price_range * 44, 2, 44) : 2
  drawWideLine(120+72*cos(a), 120+72*sin(a),
               120+(72+h)*cos(a), 120+(72+h)*sin(a),
               3, color)
```

`price_range = max(day_max - day_min, 1.0f)`  
`day_min` clamped to `max(actual_min, -10.0f)` — prevents wildly negative plunge prices
from compressing all other bars to zero.

### Current slot triangle
Small filled white triangle pointing radially outward at radius 118, current slot angle.
Base is 6 px wide (3 px each side of center ray), 6 px deep. Drawn with `fillTriangle`.

### Center text
Uses `displayFontSetSmoothSize()` with `ui_font.vlw` (Noto Sans Bold), `middle_center` datum.

```
Line 1 (~22 px tall) at y=113: "24.5p"    ← current price; "--p" if unavailable
Line 2 (~12 px tall) at y=134: "14:30"    ← current slot start time in UK local time
```

### Public API
```cpp
void priceDisplayInit();    // allocate sprite (same ensureFrameSprite pattern)
void priceDisplayDraw();    // full redraw → pushSprite
void priceDisplayUpdate();  // no-op if slot index unchanged; else priceDisplayDraw()
```

---

## Phase 4 — Main Loop (`src/main.cpp`)

### setup()
```
displayInit()
bootButtonInit()
wifiSetupConnect()                  // portal shows region field
timeSyncInit()
showSpinner until timeSyncWait(10000) or timeout
octopus::setPollFn(wifiLoop)
octopus::discoverProductCode()
octopus::fetchTodayPrices(services::wifi::region())
priceDisplayInit()
priceDisplayDraw()
```

### loop()
```
wifiLoop()
bootButtonPollLongPress()           // hold 3 s → reset config + reboot (unchanged)

if bootButtonConsumeTap():
  fetchTodayPrices(region)          // short tap = force refresh
  priceDisplayDraw()

WiFi reconnect logic (unchanged timing/grace constants)

if WiFi up:
  mod = minuteOfDay()
  if (mod == 0 || mod == 980):      // midnight or 16:20 UK time
    if (mod != g_last_fetch_minute):
      g_last_fetch_minute = mod
      fetchTodayPrices(region)
      priceDisplayDraw()

  priceDisplayUpdate()              // redraws every 30 min when slot index changes

delay(10)
```

---

## Files Deleted
- `src/services/adsb_client.cpp` + `include/services/adsb_client.h`
- `src/services/radar_location.cpp` + `include/services/radar_location.h`
- `src/ui/radar_display.cpp` + `include/ui/radar_display.h`
- `src/ui/radar_range.cpp` + `include/ui/radar_range.h`
- `src/ui/runway_overlay.cpp` + `include/ui/runway_overlay.h`
- `include/ui/radar_theme.h`
- `include/data/large_airports.h`
- `src/data/large_airports_data.cpp`
- `scripts/build_large_airports.py`

---

## Verification
1. **Portal**: Connect to AP, confirm region field with label text; save `C`
2. **Serial**: NTP sync → product discovery logs `AGILE-FLEX-*` → price fetch logs `48 slots`
3. **Display**: 48 bars, green/grey/red distribution; white-outlined current slot; white triangle; p/kWh + time in center
4. **Short tap**: Force-refresh logged and display redraws
5. **Long hold**: Clears config, reboots into portal (unchanged)
6. **Midnight**: Prices reload for new day
7. **Post-16:20**: Re-fetch populates previously `available=false` slots
