#include "services/octopus_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>
#include <Preferences.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <time.h>

#include "config.h"
#include "services/time_sync.h"

namespace services::octopus {

namespace {

constexpr char kOctopusNs[]   = "octopus";
constexpr char kProductKey[]  = "product";
constexpr int  kConnectAttemptMs = 200;

PollFn s_poll_fn = nullptr;
PriceSlot s_slots[kSlotCount];
char s_product_code[33] = "";

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long deadline = millis() + config::kPriceFetchTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

bool readResponseBodyWithPoll(HTTPClient& http, String& payload) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  if (content_length > 0) {
    payload.reserve(static_cast<unsigned>(content_length + 1));
  }

  uint8_t buffer[512];
  const unsigned long deadline = millis() + config::kPriceFetchTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int available = stream->available();
    if (available > 0) {
      const int to_read = available > static_cast<int>(sizeof(buffer))
                              ? static_cast<int>(sizeof(buffer))
                              : available;
      const int read_bytes = stream->readBytes(buffer, to_read);
      if (read_bytes > 0) {
        payload.concat(reinterpret_cast<const char*>(buffer),
                       static_cast<unsigned>(read_bytes));
      }
    }
    if (content_length > 0 &&
        static_cast<int>(payload.length()) >= content_length) {
      break;
    }
    if (!http.connected() && stream->available() <= 0) {
      break;
    }
    delay(1);
  }

  return payload.length() > 0;
}

bool httpGet(const String& url, String& payload) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("octopus: http.begin failed");
    return false;
  }

  http.setTimeout(config::kPriceFetchTimeoutMs);
  const int code = performGetWithPoll(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("octopus: HTTP %d for %s\n", code, url.c_str());
    http.end();
    return false;
  }

  const bool ok = readResponseBodyWithPoll(http, payload);
  http.end();
  if (!ok) {
    Serial.println("octopus: empty response");
  }
  return ok;
}

// Compute how many minutes UK local time is ahead of UTC right now.
int utcOffsetMinutes() {
  const time_t now = time(nullptr);
  struct tm local_t, utc_t;
  localtime_r(&now, &local_t);
  gmtime_r(&now, &utc_t);
  int diff = (local_t.tm_hour * 60 + local_t.tm_min) -
             (utc_t.tm_hour  * 60 + utc_t.tm_min);
  if (diff >  720) diff -= 1440;
  if (diff < -720) diff += 1440;
  return diff;
}

int parseUtcToSlotIndex(const char* valid_from, int offset_min) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  if (sscanf(valid_from, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 5) {
    return -1;
  }
  int local_min = h * 60 + mi + offset_min;
  if (local_min < 0)    local_min += 1440;
  if (local_min >= 1440) local_min -= 1440;
  return local_min / 30;
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

bool discoverProductCode() {
  String url = config::kOctopusApiBase;
  url += "/products/?is_variable=true&brand=OCTOPUS_ENERGY&page_size=100";

  String payload;
  if (httpGet(url, payload)) {
    JsonDocument doc;
    if (!deserializeJson(doc, payload)) {
      JsonArray results = doc["results"].as<JsonArray>();
      for (JsonObject p : results) {
        const char* name   = p["display_name"] | "";
        const char* dir    = p["direction"]    | "";
        const bool prepay  = p["is_prepay"]    | true;
        if (strcmp(name, "Agile Octopus") == 0 &&
            strcmp(dir, "IMPORT") == 0 &&
            !prepay) {
          const char* code = p["code"] | "";
          if (code[0] != '\0') {
            strncpy(s_product_code, code, sizeof(s_product_code) - 1);
            s_product_code[sizeof(s_product_code) - 1] = '\0';
            Preferences prefs;
            if (prefs.begin(kOctopusNs, false)) {
              prefs.putString(kProductKey, s_product_code);
              prefs.end();
            }
            Serial.printf("octopus: product %s\n", s_product_code);
            return true;
          }
        }
      }
    }
  }

  // API failed — try NVS cache
  Preferences prefs;
  if (prefs.begin(kOctopusNs, true)) {
    String cached = prefs.getString(kProductKey, "");
    prefs.end();
    if (cached.length() > 0) {
      strncpy(s_product_code, cached.c_str(), sizeof(s_product_code) - 1);
      s_product_code[sizeof(s_product_code) - 1] = '\0';
      Serial.printf("octopus: using cached product %s\n", s_product_code);
      return true;
    }
  }

  // Hardcoded fallback
  strncpy(s_product_code, config::kOctopusFallbackProduct, sizeof(s_product_code) - 1);
  s_product_code[sizeof(s_product_code) - 1] = '\0';
  Serial.printf("octopus: using fallback product %s\n", s_product_code);
  return false;
}

const char* productCode() {
  if (s_product_code[0] != '\0') {
    return s_product_code;
  }
  return config::kOctopusFallbackProduct;
}

bool fetchTodayPrices(char region) {
  char from_buf[32], to_buf[32];
  buildPeriodStrings(from_buf, to_buf, sizeof(from_buf));

  const char* prod = productCode();
  const char region_uc = static_cast<char>(toupper(static_cast<unsigned char>(region)));

  String url = config::kOctopusApiBase;
  url += "/products/";
  url += prod;
  url += "/electricity-tariffs/E-1R-";
  url += prod;
  url += "-";
  url += region_uc;
  url += "/standard-unit-rates/?period_from=";
  url += from_buf;
  url += "&period_to=";
  url += to_buf;
  url += "&page_size=48";

  String payload;
  if (!httpGet(url, payload)) {
    Serial.println("octopus: price fetch failed — retaining stale data");
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    Serial.println("octopus: JSON parse error — retaining stale data");
    return false;
  }

  // Reset slots only after we have a parseable response
  for (int i = 0; i < kSlotCount; ++i) {
    s_slots[i].available = false;
    s_slots[i].value_inc_vat = 0.0f;
  }

  const int offset_min = utcOffsetMinutes();
  JsonArray results = doc["results"].as<JsonArray>();
  int count = 0;
  for (JsonObject slot : results) {
    const float price        = slot["value_inc_vat"] | 0.0f;
    const char* valid_from   = slot["valid_from"] | "";
    const int   slot_idx     = parseUtcToSlotIndex(valid_from, offset_min);
    if (slot_idx >= 0 && slot_idx < kSlotCount) {
      s_slots[slot_idx].value_inc_vat = price;
      s_slots[slot_idx].available = true;
      ++count;
    }
  }

  Serial.printf("octopus: %d slots fetched\n", count);
  return true;
}

const PriceSlot* priceSlots() { return s_slots; }

float currentSlotPrice() { return s_slots[currentSlotIndex()].value_inc_vat; }

}  // namespace services::octopus
