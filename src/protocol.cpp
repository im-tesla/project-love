#include "protocol.h"

#include <ArduinoJson.h>

#include <stdio.h>
#include <string.h>

namespace protocol {
namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int8_t base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
  if (c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a' + 26);
  if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0' + 52);
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

template <typename T>
T clampValue(T value, T low, T high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

}  // namespace

// --- reassembly -------------------------------------------------------------

void Reassembler::reset() {
  buffer_.clear();
  expectedTotal_ = 0;
  nextIndex_ = 0;
  active_ = false;
}

void Reassembler::expire(uint32_t nowMs) {
  if (active_ && (nowMs - startedMs_) > LOVE_CHUNK_TIMEOUT_MS) {
    reset();
  }
}

Reassembler::Result Reassembler::accept(const uint8_t *data, size_t len, uint32_t nowMs) {
  if (data == nullptr || len < kHeaderSize) {
    reset();
    return Result::Invalid;
  }

  const uint8_t index = data[0];
  const uint8_t total = data[1];

  if (total == 0 || index >= total) {
    reset();
    return Result::Invalid;
  }

  if (index == 0) {
    // A fresh message always wins, so a retransmit after a dropped chunk just
    // works rather than colliding with the stalled attempt.
    buffer_.clear();
    expectedTotal_ = total;
    nextIndex_ = 0;
    startedMs_ = nowMs;
    active_ = true;
  } else if (!active_ || index != nextIndex_ || total != expectedTotal_) {
    // Out of sequence, or a continuation with no beginning.
    reset();
    return Result::Invalid;
  }

  const size_t payload = len - kHeaderSize;
  if (buffer_.size() + payload > kMaxMessageBytes) {
    reset();
    return Result::Overflow;
  }

  buffer_.append(reinterpret_cast<const char *>(data + kHeaderSize), payload);
  nextIndex_ = static_cast<uint8_t>(index + 1);

  if (nextIndex_ >= expectedTotal_) {
    active_ = false;
    return Result::Complete;
  }
  return Result::NeedMore;
}

// --- time strings -----------------------------------------------------------

bool parseClockTime(const char *text, uint16_t &minutesOut) {
  if (text == nullptr) {
    return false;
  }
  // Exactly "HH:MM".
  if (strlen(text) != 5 || text[2] != ':') {
    return false;
  }
  for (int i : {0, 1, 3, 4}) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
  }
  const int hours = (text[0] - '0') * 10 + (text[1] - '0');
  const int minutes = (text[3] - '0') * 10 + (text[4] - '0');
  if (hours > 23 || minutes > 59) {
    return false;
  }
  minutesOut = static_cast<uint16_t>(hours * 60 + minutes);
  return true;
}

void formatClockTime(uint16_t minutes, char *out, size_t outSize) {
  if (out == nullptr || outSize < 6) {
    return;
  }
  const uint16_t clamped = minutes % 1440;
  snprintf(out, outSize, "%02u:%02u", clamped / 60, clamped % 60);
}

// --- base64 -----------------------------------------------------------------

size_t decodeBase64(const char *text, size_t len, uint8_t *out, size_t outSize) {
  if (text == nullptr || out == nullptr) {
    return 0;
  }
  // Ignore trailing padding; length must otherwise be a multiple of 4.
  while (len > 0 && text[len - 1] == '=') {
    len--;
  }
  if (len % 4 == 1) {
    return 0;  // impossible length
  }

  size_t written = 0;
  uint32_t accumulator = 0;
  uint8_t bits = 0;

  for (size_t i = 0; i < len; i++) {
    const int8_t value = base64Value(text[i]);
    if (value < 0) {
      return 0;
    }
    accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (written >= outSize) {
        return 0;  // would overflow the caller's buffer
      }
      out[written++] = static_cast<uint8_t>((accumulator >> bits) & 0xFF);
    }
  }
  return written;
}

size_t encodeBase64(const uint8_t *data, size_t len, char *out, size_t outSize) {
  if (data == nullptr || out == nullptr) {
    return 0;
  }
  const size_t needed = ((len + 2) / 3) * 4;
  if (outSize < needed + 1) {
    return 0;
  }

  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t a = data[i];
    const uint32_t b = (i + 1 < len) ? data[i + 1] : 0;
    const uint32_t c = (i + 2 < len) ? data[i + 2] : 0;
    const uint32_t triple = (a << 16) | (b << 8) | c;

    out[o++] = kBase64Alphabet[(triple >> 18) & 0x3F];
    out[o++] = kBase64Alphabet[(triple >> 12) & 0x3F];
    out[o++] = (i + 1 < len) ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=';
    out[o++] = (i + 2 < len) ? kBase64Alphabet[triple & 0x3F] : '=';
  }
  out[o] = '\0';
  return o;
}

// --- commands ---------------------------------------------------------------

ApplyResult applyCommand(const char *json, size_t len, Settings &settings) {
  ApplyResult result;

  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) {
    return result;
  }
  const char *command = doc["c"].is<const char *>() ? doc["c"].as<const char *>() : nullptr;
  if (command == nullptr) {
    return result;
  }

  if (strcmp(command, "mode") == 0) {
    settings.mode = loveModeFromString(doc["v"].as<const char *>());
    result.ok = result.changed = result.needsSave = true;

  } else if (strcmp(command, "text") == 0) {
    loveCopyText(settings.text, sizeof(settings.text), doc["v"].as<const char *>());
    settings.mode = Mode::Text;
    result.ok = result.changed = result.needsSave = true;

  } else if (strcmp(command, "bright") == 0) {
    settings.brightness = clampValue<int>(doc["v"].as<int>(), 0, LOVE_BRIGHTNESS_MAX);
    result.ok = result.changed = result.needsSave = true;

  } else if (strcmp(command, "speed") == 0) {
    settings.speed = clampValue<int>(doc["v"].as<int>(), LOVE_SPEED_MIN, LOVE_SPEED_MAX);
    result.ok = result.changed = result.needsSave = true;

  } else if (strcmp(command, "anim") == 0) {
    loveCopyText(settings.animId, sizeof(settings.animId), doc["v"].as<const char *>());
    settings.mode = Mode::Anim;
    result.ok = result.changed = result.needsSave = true;

  } else if (strcmp(command, "draw") == 0) {
    const char *encoded = doc["v"].as<const char *>();
    if (encoded != nullptr) {
      uint8_t decoded[LOVE_WIDTH] = {0};
      const size_t got = decodeBase64(encoded, strlen(encoded), decoded, sizeof(decoded));
      if (got == LOVE_WIDTH) {
        memcpy(settings.drawing, decoded, LOVE_WIDTH);
        settings.mode = Mode::Draw;
        result.ok = result.changed = result.needsSave = true;
      }
    }

  } else if (strcmp(command, "playlist") == 0) {
    if (doc["v"].is<JsonArray>()) {
      JsonArray items = doc["v"].as<JsonArray>();
      uint8_t count = 0;
      for (JsonVariant item : items) {
        if (count >= LOVE_PLAYLIST_SLOTS) {
          break;
        }
        const char *value = item.as<const char *>();
        if (value == nullptr) {
          continue;
        }
        loveCopyText(settings.playlist[count], sizeof(settings.playlist[count]), value);
        count++;
      }
      // Clear the tail so a shortened playlist does not leave stale messages
      // rotating in from an earlier, longer one.
      for (uint8_t i = count; i < LOVE_PLAYLIST_SLOTS; i++) {
        settings.playlist[i][0] = '\0';
      }
      settings.playlistCount = count;

      if (doc["dwell"].is<int>()) {
        settings.playlistDwellS = clampValue<int>(doc["dwell"].as<int>(), 3, 3600);
      }
      settings.mode = Mode::Playlist;
      result.ok = result.changed = result.needsSave = true;
    }

  } else if (strcmp(command, "night") == 0) {
    if (doc["on"].is<bool>()) {
      settings.nightOn = doc["on"].as<bool>();
    }
    uint16_t minutes = 0;
    if (parseClockTime(doc["from"].as<const char *>(), minutes)) {
      settings.nightFromMin = minutes;
    }
    if (parseClockTime(doc["to"].as<const char *>(), minutes)) {
      settings.nightToMin = minutes;
    }
    if (doc["level"].is<int>()) {
      settings.nightLevel = clampValue<int>(doc["level"].as<int>(), 0, LOVE_BRIGHTNESS_MAX);
    }
    result.ok = result.changed = result.needsSave = true;

  } else if (strcmp(command, "time") == 0) {
    // The clock lives in Display, not Settings -- it must not be persisted,
    // because a stored time is wrong the moment the board is unplugged.
    result.ok = true;
    result.clockSync = true;
    result.epochSeconds = doc["v"].as<uint32_t>();
    result.tzOffsetMin = clampValue<int>(doc["tz"].as<int>(), -840, 840);

  } else if (strcmp(command, "reset") == 0) {
    loveDefaultSettings(settings);
    result.ok = result.changed = result.needsSave = result.factoryReset = true;
  }

  return result;
}

size_t serialiseState(const Settings &settings, bool hasClock, char *out, size_t outSize) {
  JsonDocument doc;

  doc["mode"] = loveModeToString(settings.mode);
  doc["text"] = settings.text;
  doc["bright"] = settings.brightness;
  doc["speed"] = settings.speed;
  doc["anim"] = settings.animId;

  char drawing[64];
  if (encodeBase64(settings.drawing, LOVE_WIDTH, drawing, sizeof(drawing)) > 0) {
    doc["draw"] = drawing;
  }

  JsonArray playlist = doc["playlist"].to<JsonArray>();
  for (uint8_t i = 0; i < settings.playlistCount && i < LOVE_PLAYLIST_SLOTS; i++) {
    playlist.add(settings.playlist[i]);
  }
  doc["dwell"] = settings.playlistDwellS;

  JsonObject night = doc["night"].to<JsonObject>();
  night["on"] = settings.nightOn;
  char from[6];
  char to[6];
  formatClockTime(settings.nightFromMin, from, sizeof(from));
  formatClockTime(settings.nightToMin, to, sizeof(to));
  night["from"] = from;
  night["to"] = to;
  night["level"] = settings.nightLevel;

  // Lets the UI explain why night mode is not doing anything yet.
  doc["clock"] = hasClock;

  const size_t written = serializeJson(doc, out, outSize);
  return (written > 0 && written < outSize) ? written : 0;
}

}  // namespace protocol
