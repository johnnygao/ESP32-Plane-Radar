#include "services/octopus_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cctype>
#include <cstdio>
#include <time.h>

#include "config.h"
#include "services/time_sync.h"

namespace services::octopus {

namespace {

constexpr int    kConnectAttemptMs = 200;
constexpr time_t kSlotSeconds = 30 * 60;

PollFn s_poll_fn = nullptr;
PriceSlot s_slots[kSlotCount];

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

// Days since 1970-01-01 for a proleptic Gregorian date (newlib has no timegm).
long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// Parses "YYYY-MM-DDTHH:MM:SSZ" (UTC); returns false for null/malformed.
bool parseUtc(const char* iso, time_t* out) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
  if (iso == nullptr ||
      sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 5) {
    return false;
  }
  *out = static_cast<time_t>(daysFromCivil(y, mo, d)) * 86400 +
         h * 3600 + mi * 60 + s;
  return true;
}

int localSlotIndex(time_t t) {
  struct tm local_t;
  localtime_r(&t, &local_t);
  return (local_t.tm_hour * 60 + local_t.tm_min) / 30;
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

const char* productCode() { return config::kOctopusProduct; }

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
  url += "&page_size=100";

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

  time_t day_start = 0, day_end = 0;
  todayBoundsUtc(&day_start, &day_end);

  // Cosy rates span several hours; expand each into its 30-min slots.
  // A null valid_from/valid_to means open-ended.
  JsonArray results = doc["results"].as<JsonArray>();
  int count = 0;
  for (JsonObject rate : results) {
    const float price = rate["value_inc_vat"] | 0.0f;
    time_t from = day_start, to = day_end;
    parseUtc(rate["valid_from"].as<const char*>(), &from);
    parseUtc(rate["valid_to"].as<const char*>(), &to);
    if (from < day_start) from = day_start;
    if (to > day_end) to = day_end;
    for (time_t t = from; t < to; t += kSlotSeconds) {
      const int slot_idx = localSlotIndex(t);
      if (!s_slots[slot_idx].available) {
        ++count;
      }
      s_slots[slot_idx].value_inc_vat = price;
      s_slots[slot_idx].available = true;
    }
  }

  Serial.printf("octopus: %d slots fetched\n", count);
  return true;
}

const PriceSlot* priceSlots() { return s_slots; }

float currentSlotPrice() { return s_slots[currentSlotIndex()].value_inc_vat; }

}  // namespace services::octopus
