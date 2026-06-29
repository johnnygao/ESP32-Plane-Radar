/**
 * Octopus Agile price display — WiFi setup, NTP sync, then price UI on the
 * round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "hardware/display.h"
#include "services/octopus_client.h"
#include "services/time_sync.h"
#include "services/wifi_setup.h"
#include "ui/price_display.h"
#include "ui/status_screens.h"

namespace {

bool g_display_ready = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
int g_last_fetch_minute = -1;

void fetchAndRedraw() {
  services::octopus::fetchTodayPrices(services::wifi::region());
  priceDisplayDraw();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Octopus Agile Display");

  displayInit();
  bootButtonInit();
  priceDisplayInit();  // colors + sprite; does not need WiFi

  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }

  if (!wifiSetupConnect()) {
    // Stay in error screen; loop() will retry reconnect
    return;
  }

  // NTP sync with spinner
  timeSyncInit();
  statusScreenConnectingBegin("NTP sync");
  const uint32_t ntp_deadline = millis() + 10000;
  while (millis() < ntp_deadline) {
    if (time(nullptr) > 1000000000UL) {
      Serial.println("NTP synced");
      break;
    }
    statusScreenConnectingTick();
    delay(config::kWifiConnectingFrameMs);
  }
  if (time(nullptr) <= 1000000000UL) {
    Serial.println("NTP timeout — continuing without sync");
  }

  services::octopus::setPollFn(wifiLoop);
  services::octopus::discoverProductCode();
  services::octopus::fetchTodayPrices(services::wifi::region());
  priceDisplayDraw();
  g_display_ready = true;
}

void loop() {
  bootButtonPollLongPress();
  wifiLoop();

  if (bootButtonConsumeTap()) {
    Serial.println("Tap — force refresh");
    fetchAndRedraw();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (g_display_ready) {
      Serial.println("WiFi lost — will reconnect");
      g_display_ready = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        g_display_ready = true;
      }
    }
  } else {
    g_wifi_down_since = 0;

    if (!g_display_ready) {
      fetchAndRedraw();
      g_display_ready = true;
    } else {
      const int mod = minuteOfDay();
      if (mod == 0 || mod == 980) {  // midnight or 16:20 UK
        if (mod != g_last_fetch_minute) {
          g_last_fetch_minute = mod;
          fetchAndRedraw();
        }
      }
      priceDisplayUpdate();
    }
  }

  delay(10);
}
