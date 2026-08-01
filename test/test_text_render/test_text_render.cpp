// Host-side tests for UTF-8 decoding and text rasterisation.
//
//   pio test -e native
//
// The decoder is the boundary between "bytes that arrived over BLE" and "what
// gets lit up", so it is tested against hostile input as well as Polish.

#include <unity.h>

#include <cstring>
#include <string>

#include "text_render.h"

using namespace textrender;

namespace {

size_t decodeAll(const std::string &s, std::vector<uint32_t> &out) {
  size_t pos = 0;
  while (pos < s.size()) {
    out.push_back(decodeUtf8(s.data(), s.size(), pos));
  }
  return out.size();
}

uint32_t firstCodepoint(const std::string &s) {
  size_t pos = 0;
  return decodeUtf8(s.data(), s.size(), pos);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- UTF-8, the happy path --------------------------------------------------

void test_decodes_ascii(void) {
  TEST_ASSERT_EQUAL_UINT32('K', firstCodepoint("Kocham"));
}

void test_decodes_two_byte_polish(void) {
  // U+0119 e-ogonek, encoded as C4 99
  TEST_ASSERT_EQUAL_UINT32(0x0119, firstCodepoint("\xC4\x99"));
}

void test_decodes_three_byte_heart(void) {
  // U+2665, encoded as E2 99 A5
  TEST_ASSERT_EQUAL_UINT32(0x2665, firstCodepoint("\xE2\x99\xA5"));
}

void test_decodes_a_full_polish_sentence(void) {
  // "Zażółć gęślą jaźń" -- the classic Polish pangram for diacritics.
  const std::string s = "Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87 g\xC4\x99\xC5\x9Bl\xC4\x85 ja\xC5\xBA\xC5\x84";
  std::vector<uint32_t> cps;
  decodeAll(s, cps);

  TEST_ASSERT_EQUAL_UINT32(0x005A, cps[0]);  // Z
  TEST_ASSERT_EQUAL_UINT32(0x0061, cps[1]);  // a
  TEST_ASSERT_EQUAL_UINT32(0x017C, cps[2]);  // z-dot
  TEST_ASSERT_EQUAL_UINT32(0x00F3, cps[3]);  // o-acute
  TEST_ASSERT_EQUAL_UINT32(0x0142, cps[4]);  // l-stroke
  TEST_ASSERT_EQUAL_UINT32(0x0107, cps[5]);  // c-acute

  // 17 characters from 24 bytes.
  TEST_ASSERT_EQUAL_size_t(17, cps.size());
  TEST_ASSERT_EQUAL_size_t(24, s.size());
  TEST_ASSERT_EQUAL_size_t(17, countChars(s.data(), s.size()));
}

// --- UTF-8, hostile input ---------------------------------------------------
//
// Every one of these must terminate and must advance, or a malformed BLE
// chunk becomes an infinite loop on the device.

void test_truncated_sequence_does_not_hang(void) {
  // Leading byte of a 2-byte sequence with nothing after it -- exactly what a
  // chunk boundary landing mid-character looks like.
  const std::string s = "a\xC4";
  std::vector<uint32_t> cps;
  decodeAll(s, cps);
  TEST_ASSERT_EQUAL_size_t(2, cps.size());
  TEST_ASSERT_EQUAL_UINT32('a', cps[0]);
  TEST_ASSERT_EQUAL_UINT32(kReplacement, cps[1]);
}

void test_bare_continuation_byte_is_rejected(void) {
  TEST_ASSERT_EQUAL_UINT32(kReplacement, firstCodepoint("\x99"));
}

void test_overlong_encoding_is_rejected(void) {
  // C0 80 is an overlong NUL -- a classic filter-bypass encoding.
  TEST_ASSERT_EQUAL_UINT32(kReplacement, firstCodepoint("\xC0\x80"));
  // E0 80 80 is an overlong form too.
  TEST_ASSERT_EQUAL_UINT32(kReplacement, firstCodepoint("\xE0\x80\x80"));
}

void test_surrogate_is_rejected(void) {
  // ED A0 80 would decode to U+D800, which is not a valid scalar value.
  TEST_ASSERT_EQUAL_UINT32(kReplacement, firstCodepoint("\xED\xA0\x80"));
}

void test_out_of_range_lead_byte_is_rejected(void) {
  TEST_ASSERT_EQUAL_UINT32(kReplacement, firstCodepoint("\xF5\x80\x80\x80"));
  TEST_ASSERT_EQUAL_UINT32(kReplacement, firstCodepoint("\xFF"));
}

void test_decoder_always_advances(void) {
  // Fuzz every single byte value as a lone input: none may leave pos at 0.
  for (int b = 0; b < 256; b++) {
    char byte = static_cast<char>(b);
    size_t pos = 0;
    decodeUtf8(&byte, 1, pos);
    TEST_ASSERT_TRUE_MESSAGE(pos > 0, "decoder failed to advance -- would hang");
  }
}

void test_empty_input_is_safe(void) {
  size_t pos = 0;
  TEST_ASSERT_EQUAL_UINT32(kReplacement, decodeUtf8("", 0, pos));
  TEST_ASSERT_EQUAL_size_t(0, countChars("", 0));
  TEST_ASSERT_EQUAL_size_t(0, measure("", 0));
  TEST_ASSERT_EQUAL_size_t(0, render("", 0).size());
}

// --- rasterising ------------------------------------------------------------

void test_measure_matches_render_width(void) {
  const char *s = "Kocham";
  const size_t len = strlen(s);
  TEST_ASSERT_EQUAL_size_t(measure(s, len), render(s, len).size());
}

void test_measure_has_no_trailing_spacing(void) {
  // 'T' is 5 wide. One glyph must measure exactly 5, not 5 + spacing.
  TEST_ASSERT_EQUAL_size_t(5, measure("T", 1));
  // Two glyphs: 5 + spacing + 5.
  TEST_ASSERT_EQUAL_size_t(5 + LOVE_FONT_SPACING + 5, measure("TT", 2));
}

void test_polish_text_is_not_silently_narrowed(void) {
  // If UTF-8 were treated as bytes, "cc" (2 bytes) and c-acute (2 bytes) would
  // rasterise to the same width. They must not.
  const std::string accented = "\xC4\x87";  // c-acute, one character
  const std::string plain = "cc";           // two characters
  TEST_ASSERT_EQUAL_size_t(2, plain.size());
  TEST_ASSERT_EQUAL_size_t(2, accented.size());
  TEST_ASSERT_NOT_EQUAL(measure(plain.data(), plain.size()),
                        measure(accented.data(), accented.size()));
}

void test_unknown_character_falls_back_to_notdef(void) {
  // U+4E2D is a CJK character with no glyph; it must render as .notdef rather
  // than vanishing or crashing.
  const std::string cjk = "\xE4\xB8\xAD";
  const std::vector<uint8_t> columns = render(cjk.data(), cjk.size());
  TEST_ASSERT_GREATER_THAN_size_t(0, columns.size());
  bool anyLit = false;
  for (uint8_t c : columns) anyLit |= (c != 0);
  TEST_ASSERT_TRUE(anyLit);
}

void test_centred_text_is_exactly_the_requested_width(void) {
  const std::vector<uint8_t> out = renderCentred("Hi", 2, 32);
  TEST_ASSERT_EQUAL_size_t(32, out.size());
}

void test_centred_text_is_actually_centred(void) {
  // 'T' is 5 wide in a 32 column window -> 13 blank, 5 glyph, 14 blank.
  const std::vector<uint8_t> out = renderCentred("T", 1, 32);
  for (size_t i = 0; i < 13; i++) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, out[i], "left padding should be blank");
  }
  TEST_ASSERT_NOT_EQUAL(0, out[13]);
  for (size_t i = 18; i < 32; i++) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, out[i], "right padding should be blank");
  }
}

void test_centred_text_wider_than_window_is_clipped_not_overflowing(void) {
  const char *s = "This message is far too long to sit still";
  const std::vector<uint8_t> out = renderCentred(s, strlen(s), 32);
  TEST_ASSERT_EQUAL_size_t(32, out.size());
}

void test_scrolling_bitmap_appends_the_gap(void) {
  const std::vector<uint8_t> out = renderScrolling("T", 1, 8);
  TEST_ASSERT_EQUAL_size_t(5 + 8, out.size());
  for (size_t i = 5; i < out.size(); i++) {
    TEST_ASSERT_EQUAL_UINT8(0, out[i]);
  }
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_decodes_ascii);
  RUN_TEST(test_decodes_two_byte_polish);
  RUN_TEST(test_decodes_three_byte_heart);
  RUN_TEST(test_decodes_a_full_polish_sentence);

  RUN_TEST(test_truncated_sequence_does_not_hang);
  RUN_TEST(test_bare_continuation_byte_is_rejected);
  RUN_TEST(test_overlong_encoding_is_rejected);
  RUN_TEST(test_surrogate_is_rejected);
  RUN_TEST(test_out_of_range_lead_byte_is_rejected);
  RUN_TEST(test_decoder_always_advances);
  RUN_TEST(test_empty_input_is_safe);

  RUN_TEST(test_measure_matches_render_width);
  RUN_TEST(test_measure_has_no_trailing_spacing);
  RUN_TEST(test_polish_text_is_not_silently_narrowed);
  RUN_TEST(test_unknown_character_falls_back_to_notdef);
  RUN_TEST(test_centred_text_is_exactly_the_requested_width);
  RUN_TEST(test_centred_text_is_actually_centred);
  RUN_TEST(test_centred_text_wider_than_window_is_clipped_not_overflowing);
  RUN_TEST(test_scrolling_bitmap_appends_the_gap);

  return UNITY_END();
}
