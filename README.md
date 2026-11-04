# Octopus Cosy Price Display

Firmware for an **ESP32-C3 Super Mini** and a **1.28″ round GC9A01** display (240×240). Shows today's **Cosy Octopus** (`COSY-22-12-08`) electricity unit rates as 48 half-hourly bars on a circular chart, colour-coded by rate tier.

## What it shows

48 bars arranged around the clock face — one per half-hour slot, starting at midnight (12 o'clock) and running clockwise. The current slot is highlighted with a white border and an inward-pointing triangle pointer. The centre shows:

- **Large** — current price in pence (e.g. `24.5p`), or `--p` if data is unavailable
- **Small** — slot start time (e.g. `14:30`)

### Colour coding

Each bar is coloured by where its rate sits among the day's rates:

|     Colour     |                   Meaning                    |
|----------------|----------------------------------------------|
| Green          | Day's lowest rate — Cosy (cheap) window      |
| Grey           | Any rate in between — standard               |
| Red            | Day's highest rate — peak (16:00–19:00)      |
| Teal           | Negative price                               |
| Dark grey stub | No rate for this slot (e.g. API unreachable) |

## First-time setup

1. Power on — the display shows a yellow setup screen
2. Join the Wi-Fi network **`OctopusCosy-Setup`** from your phone or laptop
3. Open **`http://octopus-cosy.local`** or **`http://192.168.4.1`**
4. Enter your home Wi-Fi credentials and your **DNO region** letter (see table below; defaults to `H`)
5. Save — the device connects, syncs time, fetches today's prices, and draws the display

### DNO regions

| Letter | Region |
|--------|--------|
| A | East England |
| B | East Midlands |
| C | London |
| D | Merseyside & North Wales |
| E | West Midlands |
| F | North East |
| G | North West |
| H | South England |
| J | South East |
| K | South Wales |
| L | South West |
| M | Yorkshire |
| N | South Scotland |
| P | North Scotland |

### Reconfiguring

Visit **`http://octopus-cosy.local`** (or the device IP shown in the serial log) while it's connected to Wi-Fi.

## Controls (BOOT button, GPIO 9)

| Action | Effect |
|--------|--------|
| **Short tap** | Force-refresh prices from the API immediately |
| **Hold 3 s** | Clear Wi-Fi credentials and DNO region; reboot into setup portal |

## Price refresh schedule

- **Midnight UK time** — fetches the new day's rates
- **Short tap** — manual refresh at any time

The product code lives in `include/config.h` (`kOctopusProduct`).

## Wiring (GC9A01 ↔ ESP32-C3 Super Mini)

| Display pin | ESP32-C3 GPIO |
|-------------|--------------|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO **0** |
| CS | GPIO **1** |
| DC | GPIO **10** |
| SDA (MOSI) | GPIO **3** |
| SCL (SCLK) | GPIO **4** |
| BLK | 3V3 (backlight always on) |

The BOOT button is on GPIO **9** (built into the Super Mini board).

Orient the board so the USB-C port faces **down**.

## Build & flash

```bash
python3 -m platformio run --target upload
python3 -m platformio device monitor
```

- PlatformIO env: **`supermini`**
- Serial: **115200** baud
- Upload protocol: `esptool` (native USB, auto-reset via 1200 bps touch)

## Project layout

```
include/
  config.h                    — pin assignments, API endpoints, timing constants
  hardware/
    lgfx_config.hpp
    display.h
    display_font.h
  ui/
    price_theme.h             — geometry and colour constants
    price_display.h
    status_screens.h
  services/
    wifi_setup.h              — WiFiManager wrapper; exposes region()
    octopus_client.h          — Octopus Cosy API client
    time_sync.h               — NTP + slot index helpers
data/
  ui_font.vlw                 — embedded VLW font (status screens)
src/
  main.cpp
  hardware/
  ui/
    price_display.cpp
    status_screens.cpp
  services/
    wifi_setup.cpp
    octopus_client.cpp
    time_sync.cpp
```

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
