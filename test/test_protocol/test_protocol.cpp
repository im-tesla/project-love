// Host-side tests for the BLE wire contract.
//
//   pio test -e native
//
// Chunk reassembly gets the most attention here because it is the one part of
// this project that fails silently. A dropped chunk does not crash anything --
// it quietly turns her message into something else, which is exactly the kind
// of bug that is miserable to chase with only an LED matrix for output.

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "protocol.h"
#include "state.h"

using namespace protocol;

namespace {

using Result = Reassembler::Result;

// Unity's TEST_ASSERT_EQUAL works on integers, and these are scoped enums, so
// everything crosses over to int at the boundary.
constexpr int kNeedMore = static_cast<int>(Result::NeedMore);
constexpr int kComplete = static_cast<int>(Result::Complete);
constexpr int kInvalid = static_cast<int>(Result::Invalid);
constexpr int kOverflow = static_cast<int>(Result::Overflow);

constexpr int asInt(Mode mode) { return static_cast<int>(mode); }

// Builds one framed chunk: [index, total] + payload.
std::vector<uint8_t> frame(uint8_t index, uint8_t total, const std::string &payload) {
  std::vector<uint8_t> out;
  out.push_back(index);
  out.push_back(total);
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

int feed(Reassembler &r, const std::vector<uint8_t> &chunk, uint32_t nowMs = 0) {
  return static_cast<int>(r.accept(chunk.data(), chunk.size(), nowMs));
}

// Splits a message the way the phone would, given a payload budget.
std::vector<std::vector<uint8_t>> split(const std::string &message, size_t perChunk) {
  const size_t total = (message.size() + perChunk - 1) / perChunk;
  std::vector<std::vector<uint8_t>> chunks;
  for (size_t i = 0; i < total; i++) {
    chunks.push_back(frame(static_cast<uint8_t>(i), static_cast<uint8_t>(total),
                           message.substr(i * perChunk, perChunk)));
  }
  return chunks;
}

ApplyResult applyJson(const std::string &json, Settings &s) {
  return applyCommand(json.data(), json.size(), s);
}

Settings freshSettings() {
  Settings s;
  loveDefaultSettings(s);
  return s;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- reassembly, the happy path ---------------------------------------------

void test_single_chunk_completes_immediately(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kComplete, feed(r, frame(0, 1, "hello")));
  TEST_ASSERT_EQUAL_STRING("hello", r.message());
  TEST_ASSERT_EQUAL_size_t(5, r.messageLength());
}

void test_two_chunks_are_joined_in_order(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kNeedMore, feed(r, frame(0, 2, "Kocham ")));
  TEST_ASSERT_EQUAL(kComplete, feed(r, frame(1, 2, "Cie")));
  TEST_ASSERT_EQUAL_STRING("Kocham Cie", r.message());
}

void test_a_realistic_long_polish_message_survives_chunking(void) {
  // 200+ bytes of multi-byte UTF-8 split at an iOS-sized MTU. This is the
  // scenario the whole framing scheme exists for.
  //
  // Note "ka\xC5\xBC" "dy": a hex escape swallows every following hex digit,
  // so \xBC immediately before 'd' would be read as \xBCD and overflow.
  std::string message =
      "{\"c\":\"text\",\"v\":\"Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 g\xC4\x99\xC5\x9Bl\xC4\x85 "
      "ja\xC5\xBA\xC5\x84, kocham Ci\xC4\x99 bardzo mocno i na zawsze, moja "
      "najpi\xC4\x99kniejsza Mileno \xE2\x99\xA5 Jeste\xC5\x9B moim ca\xC5\x82ym "
      "\xC5\x9Bwiatem i ka\xC5\xBC" "dy dzie\xC5\x84 z Tob\xC4\x85 jest "
      "pi\xC4\x99kniejszy od poprzedniego \xE2\x99\xA5\"}";
  TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(
      183, message.size(), "the test message must exceed one MTU or it proves nothing");

  Reassembler r;
  const auto chunks = split(message, 183);
  TEST_ASSERT_GREATER_THAN_size_t(1, chunks.size());

  for (size_t i = 0; i + 1 < chunks.size(); i++) {
    TEST_ASSERT_EQUAL(kNeedMore, feed(r, chunks[i]));
  }
  TEST_ASSERT_EQUAL(kComplete, feed(r, chunks.back()));
  TEST_ASSERT_EQUAL_size_t(message.size(), r.messageLength());
  TEST_ASSERT_EQUAL_STRING(message.c_str(), r.message());

  // And it still parses into the right text once reassembled.
  Settings s = freshSettings();
  const ApplyResult applied = applyCommand(r.message(), r.messageLength(), s);
  TEST_ASSERT_TRUE(applied.ok);
  TEST_ASSERT_NOT_NULL(strstr(s.text, "Mileno"));
}

void test_reassembler_is_reusable_after_completing(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kComplete, feed(r, frame(0, 1, "first")));
  TEST_ASSERT_EQUAL(kComplete, feed(r, frame(0, 1, "second")));
  TEST_ASSERT_EQUAL_STRING("second", r.message());
}

void test_empty_payload_chunks_are_allowed(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kComplete, feed(r, frame(0, 1, "")));
  TEST_ASSERT_EQUAL_size_t(0, r.messageLength());
}

// --- reassembly, everything going wrong -------------------------------------

void test_chunk_shorter_than_the_header_is_rejected(void) {
  Reassembler r;
  const uint8_t stub[1] = {0};
  TEST_ASSERT_EQUAL(kInvalid, (int)r.accept(stub, 1, 0));
  TEST_ASSERT_EQUAL(kInvalid, (int)r.accept(nullptr, 0, 0));
}

void test_zero_total_is_rejected(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kInvalid, feed(r, frame(0, 0, "x")));
}

void test_index_beyond_total_is_rejected(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kInvalid, feed(r, frame(3, 3, "x")));
}

void test_continuation_without_a_start_is_rejected(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kInvalid, feed(r, frame(1, 2, "orphan")));
}

void test_out_of_order_chunk_resets_rather_than_corrupting(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kNeedMore, feed(r, frame(0, 3, "aaa")));
  // Chunk 1 goes missing and chunk 2 arrives. Appending it would silently
  // produce a message with a hole in it.
  TEST_ASSERT_EQUAL(kInvalid, feed(r, frame(2, 3, "ccc")));
  TEST_ASSERT_FALSE(r.inProgress());
}

void test_changing_total_mid_message_is_rejected(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kNeedMore, feed(r, frame(0, 3, "aaa")));
  TEST_ASSERT_EQUAL(kInvalid, feed(r, frame(1, 4, "bbb")));
}

void test_restarting_discards_the_stalled_attempt(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kNeedMore, feed(r, frame(0, 2, "STALE")));
  // The phone gave up and resent from the top.
  TEST_ASSERT_EQUAL(kNeedMore, feed(r, frame(0, 2, "good ")));
  TEST_ASSERT_EQUAL(kComplete, feed(r, frame(1, 2, "message")));
  TEST_ASSERT_EQUAL_STRING("good message", r.message());
}

void test_oversized_message_overflows_instead_of_growing_forever(void) {
  Reassembler r;
  const std::string big(1000, 'x');
  int last = kNeedMore;
  for (uint8_t i = 0; i < 4; i++) {
    last = feed(r, frame(i, 4, big));
    if (last == kOverflow) {
      break;
    }
  }
  TEST_ASSERT_EQUAL(kOverflow, last);
  TEST_ASSERT_FALSE(r.inProgress());
}

void test_partial_message_expires(void) {
  Reassembler r;
  TEST_ASSERT_EQUAL(kNeedMore, feed(r, frame(0, 2, "half"), 1000));
  TEST_ASSERT_TRUE(r.inProgress());

  r.expire(1000 + LOVE_CHUNK_TIMEOUT_MS / 2);
  TEST_ASSERT_TRUE_MESSAGE(r.inProgress(), "should not expire before the timeout");

  r.expire(1000 + LOVE_CHUNK_TIMEOUT_MS + 1);
  TEST_ASSERT_FALSE_MESSAGE(r.inProgress(), "a phone that walked out of range must not wedge the buffer");
}

// --- commands ---------------------------------------------------------------

void test_text_command_sets_text_and_switches_mode(void) {
  Settings s = freshSettings();
  s.mode = Mode::Anim;
  const ApplyResult r = applyJson("{\"c\":\"text\",\"v\":\"Dobranoc\"}", s);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.needsSave);
  TEST_ASSERT_EQUAL_STRING("Dobranoc", s.text);
  TEST_ASSERT_EQUAL(asInt(Mode::Text), asInt(s.mode));
}

void test_polish_text_survives_the_json_round_trip(void) {
  Settings s = freshSettings();
  const ApplyResult r = applyJson("{\"c\":\"text\",\"v\":\"Kocham Ci\xC4\x99 \xE2\x99\xA5\"}", s);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_STRING("Kocham Ci\xC4\x99 \xE2\x99\xA5", s.text);
}

void test_overlong_text_is_truncated_on_a_character_boundary(void) {
  Settings s = freshSettings();
  // Far more e-ogoneks than fit. The result must not end mid-sequence.
  std::string json = "{\"c\":\"text\",\"v\":\"";
  for (int i = 0; i < 400; i++) {
    json += "\xC4\x99";
  }
  json += "\"}";

  TEST_ASSERT_TRUE(applyJson(json, s).ok);
  const size_t len = strlen(s.text);
  TEST_ASSERT_LESS_THAN_size_t(LOVE_MAX_TEXT_BYTES, len);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, len % 2,
                                   "truncated mid-character -- half an e-ogonek survived");
  // Every byte must still be part of a well-formed pair.
  for (size_t i = 0; i < len; i += 2) {
    TEST_ASSERT_EQUAL_UINT8(0xC4, static_cast<uint8_t>(s.text[i]));
    TEST_ASSERT_EQUAL_UINT8(0x99, static_cast<uint8_t>(s.text[i + 1]));
  }
}

void test_brightness_and_speed_are_clamped_not_rejected(void) {
  Settings s = freshSettings();
  TEST_ASSERT_TRUE(applyJson("{\"c\":\"bright\",\"v\":999}", s).ok);
  TEST_ASSERT_EQUAL_UINT8(LOVE_BRIGHTNESS_MAX, s.brightness);

  TEST_ASSERT_TRUE(applyJson("{\"c\":\"bright\",\"v\":-5}", s).ok);
  TEST_ASSERT_EQUAL_UINT8(0, s.brightness);

  TEST_ASSERT_TRUE(applyJson("{\"c\":\"speed\",\"v\":1}", s).ok);
  TEST_ASSERT_EQUAL_UINT8(LOVE_SPEED_MIN, s.speed);

  TEST_ASSERT_TRUE(applyJson("{\"c\":\"speed\",\"v\":9999}", s).ok);
  TEST_ASSERT_EQUAL_UINT8(LOVE_SPEED_MAX, s.speed);
}

void test_anim_command_sets_id_and_mode(void) {
  Settings s = freshSettings();
  TEST_ASSERT_TRUE(applyJson("{\"c\":\"anim\",\"v\":\"sparkle\"}", s).ok);
  TEST_ASSERT_EQUAL_STRING("sparkle", s.animId);
  TEST_ASSERT_EQUAL(asInt(Mode::Anim), asInt(s.mode));
}

void test_draw_command_accepts_exactly_32_bytes(void) {
  Settings s = freshSettings();
  uint8_t pattern[LOVE_WIDTH];
  for (uint8_t i = 0; i < LOVE_WIDTH; i++) {
    pattern[i] = static_cast<uint8_t>(i * 7 + 1);
  }
  char encoded[64];
  TEST_ASSERT_GREATER_THAN_size_t(0, encodeBase64(pattern, LOVE_WIDTH, encoded, sizeof(encoded)));

  const std::string json = std::string("{\"c\":\"draw\",\"v\":\"") + encoded + "\"}";
  TEST_ASSERT_TRUE(applyJson(json, s).ok);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pattern, s.drawing, LOVE_WIDTH);
  TEST_ASSERT_EQUAL(asInt(Mode::Draw), asInt(s.mode));
}

void test_draw_command_rejects_the_wrong_length(void) {
  Settings s = freshSettings();
  Settings before = s;
  // Four bytes, not thirty-two.
  TEST_ASSERT_FALSE(applyJson("{\"c\":\"draw\",\"v\":\"AAAAAA==\"}", s).ok);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(before.drawing, s.drawing, LOVE_WIDTH);
}

void test_playlist_populates_and_clears_the_tail(void) {
  Settings s = freshSettings();
  TEST_ASSERT_TRUE(applyJson("{\"c\":\"playlist\",\"v\":[\"a\",\"b\",\"c\"],\"dwell\":45}", s).ok);
  TEST_ASSERT_EQUAL_UINT8(3, s.playlistCount);
  TEST_ASSERT_EQUAL_STRING("c", s.playlist[2]);
  TEST_ASSERT_EQUAL_UINT16(45, s.playlistDwellS);
  TEST_ASSERT_EQUAL(asInt(Mode::Playlist), asInt(s.mode));

  // Shortening it must not leave the old third message rotating in.
  TEST_ASSERT_TRUE(applyJson("{\"c\":\"playlist\",\"v\":[\"x\"]}", s).ok);
  TEST_ASSERT_EQUAL_UINT8(1, s.playlistCount);
  TEST_ASSERT_EQUAL_STRING("", s.playlist[1]);
  TEST_ASSERT_EQUAL_STRING("", s.playlist[2]);
}

void test_playlist_ignores_entries_beyond_the_slot_count(void) {
  Settings s = freshSettings();
  TEST_ASSERT_TRUE(applyJson("{\"c\":\"playlist\",\"v\":[\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\"]}", s).ok);
  TEST_ASSERT_EQUAL_UINT8(LOVE_PLAYLIST_SLOTS, s.playlistCount);
}

void test_night_command_parses_times(void) {
  Settings s = freshSettings();
  TEST_ASSERT_TRUE(
      applyJson("{\"c\":\"night\",\"on\":true,\"from\":\"22:30\",\"to\":\"07:15\",\"level\":2}", s).ok);
  TEST_ASSERT_TRUE(s.nightOn);
  TEST_ASSERT_EQUAL_UINT16(22 * 60 + 30, s.nightFromMin);
  TEST_ASSERT_EQUAL_UINT16(7 * 60 + 15, s.nightToMin);
  TEST_ASSERT_EQUAL_UINT8(2, s.nightLevel);
}

void test_night_command_ignores_malformed_times(void) {
  Settings s = freshSettings();
  const uint16_t before = s.nightFromMin;
  TEST_ASSERT_TRUE(applyJson("{\"c\":\"night\",\"from\":\"25:99\"}", s).ok);
  TEST_ASSERT_EQUAL_UINT16(before, s.nightFromMin);
}

void test_time_command_reports_a_clock_sync_without_touching_settings(void) {
  Settings s = freshSettings();
  const Settings before = s;
  const ApplyResult r = applyJson("{\"c\":\"time\",\"v\":1754049600,\"tz\":120}", s);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.clockSync);
  TEST_ASSERT_FALSE_MESSAGE(r.needsSave,
                            "a stored clock is wrong the moment the board is unplugged");
  TEST_ASSERT_EQUAL_UINT32(1754049600u, r.epochSeconds);
  TEST_ASSERT_EQUAL_INT16(120, r.tzOffsetMin);
  TEST_ASSERT_EQUAL_MEMORY(&before, &s, sizeof(Settings));
}

void test_reset_restores_defaults(void) {
  Settings s = freshSettings();
  applyJson("{\"c\":\"text\",\"v\":\"changed\"}", s);
  applyJson("{\"c\":\"bright\",\"v\":1}", s);

  const ApplyResult r = applyJson("{\"c\":\"reset\"}", s);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_TRUE(r.factoryReset);

  Settings defaults = freshSettings();
  TEST_ASSERT_EQUAL_STRING(defaults.text, s.text);
  TEST_ASSERT_EQUAL_UINT8(defaults.brightness, s.brightness);
}

void test_malformed_and_unknown_commands_are_refused_quietly(void) {
  Settings s = freshSettings();
  const Settings before = s;

  TEST_ASSERT_FALSE(applyJson("not json at all", s).ok);
  TEST_ASSERT_FALSE(applyJson("{\"c\":\"nonsense\"}", s).ok);
  TEST_ASSERT_FALSE(applyJson("{}", s).ok);
  TEST_ASSERT_FALSE(applyJson("{\"c\":42}", s).ok);
  TEST_ASSERT_FALSE(applyJson("", s).ok);

  TEST_ASSERT_EQUAL_MEMORY(&before, &s, sizeof(Settings));
}

// --- helpers ----------------------------------------------------------------

void test_clock_time_parsing(void) {
  uint16_t minutes = 0;
  TEST_ASSERT_TRUE(parseClockTime("00:00", minutes));
  TEST_ASSERT_EQUAL_UINT16(0, minutes);
  TEST_ASSERT_TRUE(parseClockTime("23:59", minutes));
  TEST_ASSERT_EQUAL_UINT16(1439, minutes);
  TEST_ASSERT_TRUE(parseClockTime("07:05", minutes));
  TEST_ASSERT_EQUAL_UINT16(425, minutes);

  TEST_ASSERT_FALSE(parseClockTime("24:00", minutes));
  TEST_ASSERT_FALSE(parseClockTime("12:60", minutes));
  TEST_ASSERT_FALSE(parseClockTime("7:05", minutes));
  TEST_ASSERT_FALSE(parseClockTime("07-05", minutes));
  TEST_ASSERT_FALSE(parseClockTime("", minutes));
  TEST_ASSERT_FALSE(parseClockTime(nullptr, minutes));
}

void test_clock_time_formatting(void) {
  char out[6];
  formatClockTime(1350, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("22:30", out);
  formatClockTime(0, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("00:00", out);
  formatClockTime(425, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("07:05", out);
}

void test_base64_round_trips(void) {
  uint8_t original[LOVE_WIDTH];
  for (uint8_t i = 0; i < LOVE_WIDTH; i++) {
    original[i] = static_cast<uint8_t>(i * 11 + 3);
  }
  char encoded[64];
  TEST_ASSERT_GREATER_THAN_size_t(0, encodeBase64(original, LOVE_WIDTH, encoded, sizeof(encoded)));

  uint8_t decoded[LOVE_WIDTH] = {0};
  TEST_ASSERT_EQUAL_size_t(LOVE_WIDTH,
                           decodeBase64(encoded, strlen(encoded), decoded, sizeof(decoded)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(original, decoded, LOVE_WIDTH);
}

void test_base64_matches_a_known_vector(void) {
  const uint8_t input[] = {'M', 'a', 'n'};
  char out[8];
  encodeBase64(input, 3, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("TWFu", out);
}

void test_base64_rejects_junk(void) {
  uint8_t out[LOVE_WIDTH];
  TEST_ASSERT_EQUAL_size_t(0, decodeBase64("!!!!", 4, out, sizeof(out)));
  TEST_ASSERT_EQUAL_size_t(0, decodeBase64("A", 1, out, sizeof(out)));
}

void test_base64_will_not_overflow_the_output_buffer(void) {
  uint8_t small[4];
  // 64 base64 chars decode to 48 bytes, which does not fit in 4.
  const std::string long64(64, 'A');
  TEST_ASSERT_EQUAL_size_t(0, decodeBase64(long64.data(), long64.size(), small, sizeof(small)));
}

// --- state serialisation ----------------------------------------------------

void test_state_serialises_every_control_the_ui_needs(void) {
  Settings s = freshSettings();
  loveCopyText(s.text, sizeof(s.text), "Test \xC4\x99");
  s.brightness = 9;
  s.speed = 60;
  s.nightOn = true;
  s.nightFromMin = 22 * 60 + 30;

  char out[kStateBufferSize];
  const size_t written = serialiseState(s, /*hasClock=*/true, out, sizeof(out));
  TEST_ASSERT_GREATER_THAN_size_t(0, written);

  TEST_ASSERT_NOT_NULL(strstr(out, "\"mode\":\"text\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"bright\":9"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"speed\":60"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"22:30\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"clock\":true"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"draw\""));
}

void test_state_reports_a_missing_clock(void) {
  Settings s = freshSettings();
  char out[kStateBufferSize];
  TEST_ASSERT_GREATER_THAN_size_t(0, serialiseState(s, /*hasClock=*/false, out, sizeof(out)));
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out, "\"clock\":false"),
                               "the UI needs this to explain why night mode is idle");
}

void test_state_fits_the_buffer_even_when_everything_is_full(void) {
  Settings s = freshSettings();
  // Worst case: a full playlist of maximum-length messages.
  std::string longest(LOVE_MAX_TEXT_BYTES - 1, 'W');
  loveCopyText(s.text, sizeof(s.text), longest.c_str());
  for (uint8_t i = 0; i < LOVE_PLAYLIST_SLOTS; i++) {
    loveCopyText(s.playlist[i], sizeof(s.playlist[i]), longest.c_str());
  }
  s.playlistCount = LOVE_PLAYLIST_SLOTS;

  char out[kStateBufferSize];
  TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(
      0, serialiseState(s, true, out, sizeof(out)),
      "kStateBufferSize is too small for a full playlist");
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_single_chunk_completes_immediately);
  RUN_TEST(test_two_chunks_are_joined_in_order);
  RUN_TEST(test_a_realistic_long_polish_message_survives_chunking);
  RUN_TEST(test_reassembler_is_reusable_after_completing);
  RUN_TEST(test_empty_payload_chunks_are_allowed);

  RUN_TEST(test_chunk_shorter_than_the_header_is_rejected);
  RUN_TEST(test_zero_total_is_rejected);
  RUN_TEST(test_index_beyond_total_is_rejected);
  RUN_TEST(test_continuation_without_a_start_is_rejected);
  RUN_TEST(test_out_of_order_chunk_resets_rather_than_corrupting);
  RUN_TEST(test_changing_total_mid_message_is_rejected);
  RUN_TEST(test_restarting_discards_the_stalled_attempt);
  RUN_TEST(test_oversized_message_overflows_instead_of_growing_forever);
  RUN_TEST(test_partial_message_expires);

  RUN_TEST(test_text_command_sets_text_and_switches_mode);
  RUN_TEST(test_polish_text_survives_the_json_round_trip);
  RUN_TEST(test_overlong_text_is_truncated_on_a_character_boundary);
  RUN_TEST(test_brightness_and_speed_are_clamped_not_rejected);
  RUN_TEST(test_anim_command_sets_id_and_mode);
  RUN_TEST(test_draw_command_accepts_exactly_32_bytes);
  RUN_TEST(test_draw_command_rejects_the_wrong_length);
  RUN_TEST(test_playlist_populates_and_clears_the_tail);
  RUN_TEST(test_playlist_ignores_entries_beyond_the_slot_count);
  RUN_TEST(test_night_command_parses_times);
  RUN_TEST(test_night_command_ignores_malformed_times);
  RUN_TEST(test_time_command_reports_a_clock_sync_without_touching_settings);
  RUN_TEST(test_reset_restores_defaults);
  RUN_TEST(test_malformed_and_unknown_commands_are_refused_quietly);

  RUN_TEST(test_clock_time_parsing);
  RUN_TEST(test_clock_time_formatting);
  RUN_TEST(test_base64_round_trips);
  RUN_TEST(test_base64_matches_a_known_vector);
  RUN_TEST(test_base64_rejects_junk);
  RUN_TEST(test_base64_will_not_overflow_the_output_buffer);

  RUN_TEST(test_state_serialises_every_control_the_ui_needs);
  RUN_TEST(test_state_reports_a_missing_clock);
  RUN_TEST(test_state_fits_the_buffer_even_when_everything_is_full);

  return UNITY_END();
}
