// Host-side tests for the generated font table.
//
//   pio test -e native
//
// build_font.py already validates font_source.json at generation time. These
// tests cover the other half: that the *generated header* behaves correctly --
// binary search, notdef fallback, and above all the column bit order, which is
// the kind of thing that looks fine in a table and comes out upside down on the
// wall.

#include <unity.h>

#include "font_love.h"

namespace {

// U+0104 U+0105 ... the nine Polish letters in both cases.
constexpr uint32_t kPolish[] = {
    0x0104, 0x0105,  // A-ogonek
    0x0106, 0x0107,  // C-acute
    0x0118, 0x0119,  // E-ogonek
    0x0141, 0x0142,  // L-stroke
    0x0143, 0x0144,  // N-acute
    0x00D3, 0x00F3,  // O-acute
    0x015A, 0x015B,  // S-acute
    0x0179, 0x017A,  // Z-acute
    0x017B, 0x017C,  // Z-dot
};
constexpr size_t kPolishCount = sizeof(kPolish) / sizeof(kPolish[0]);

// Accented letters paired with the base letter they must NOT match.
constexpr uint32_t kAccentPairs[][2] = {
    {0x0106, 'C'}, {0x0107, 'c'},  {0x0143, 'N'}, {0x0144, 'n'},
    {0x00D3, 'O'}, {0x00F3, 'o'},  {0x015A, 'S'}, {0x015B, 's'},
    {0x0179, 'Z'}, {0x017A, 'z'},  {0x017B, 'Z'}, {0x017C, 'z'},
    {0x0104, 'A'}, {0x0105, 'a'},  {0x0118, 'E'}, {0x0119, 'e'},
    {0x0141, 'L'}, {0x0142, 'l'},
};
constexpr size_t kAccentPairCount = sizeof(kAccentPairs) / sizeof(kAccentPairs[0]);

bool sameGlyph(const LoveGlyph &a, const LoveGlyph &b) {
  if (a.width != b.width) return false;
  const uint8_t *ca = loveFontColumns(a);
  const uint8_t *cb = loveFontColumns(b);
  for (uint8_t i = 0; i < a.width; i++) {
    if (ca[i] != cb[i]) return false;
  }
  return true;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- lookup -----------------------------------------------------------------

void test_finds_ascii_glyph(void) {
  const LoveGlyph *a = loveFontFind('A');
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL_UINT32('A', a->codepoint);
  TEST_ASSERT_EQUAL_UINT8(5, a->width);
}

void test_missing_codepoint_returns_null(void) {
  // U+0100 (A-macron) is not a Polish letter and has no entry.
  TEST_ASSERT_NULL(loveFontFind(0x0100));
}

void test_missing_codepoint_falls_back_to_notdef(void) {
  const LoveGlyph &fallback = loveFontGlyph(0x0100);
  TEST_ASSERT_EQUAL_UINT32(LOVE_FONT_NOTDEF, fallback.codepoint);
}

void test_binary_search_reaches_both_ends(void) {
  // Space is the lowest codepoint in the table, notdef the highest.
  const LoveGlyph *first = loveFontFind(LOVE_FONT_GLYPHS[0].codepoint);
  const LoveGlyph *last =
      loveFontFind(LOVE_FONT_GLYPHS[LOVE_FONT_GLYPH_COUNT - 1].codepoint);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(last);
  TEST_ASSERT_EQUAL_UINT32(LOVE_FONT_GLYPHS[0].codepoint, first->codepoint);
  TEST_ASSERT_EQUAL_UINT32(LOVE_FONT_GLYPHS[LOVE_FONT_GLYPH_COUNT - 1].codepoint,
                           last->codepoint);
}

void test_table_is_sorted(void) {
  for (size_t i = 1; i < LOVE_FONT_GLYPH_COUNT; i++) {
    TEST_ASSERT_TRUE_MESSAGE(
        LOVE_FONT_GLYPHS[i - 1].codepoint < LOVE_FONT_GLYPHS[i].codepoint,
        "glyph table must be strictly sorted for binary search to work");
  }
}

void test_every_glyph_stays_inside_the_column_array(void) {
  constexpr size_t kColumns = sizeof(LOVE_FONT_COLUMNS);
  for (size_t i = 0; i < LOVE_FONT_GLYPH_COUNT; i++) {
    const LoveGlyph &g = LOVE_FONT_GLYPHS[i];
    TEST_ASSERT_TRUE_MESSAGE(g.offset + g.width <= kColumns,
                             "glyph offset/width runs past the column array");
  }
}

// --- bit order --------------------------------------------------------------
//
// If these two fail together, the display is vertically mirrored and the fix
// is in build_font.py, not in the renderer.

void test_bit0_is_the_top_row(void) {
  // 'T' has a solid bar across row 0, so every column must have bit 0 set.
  const LoveGlyph &t = loveFontGlyph('T');
  const uint8_t *cols = loveFontColumns(t);
  for (uint8_t i = 0; i < t.width; i++) {
    TEST_ASSERT_TRUE_MESSAGE(cols[i] & 0x01, "'T' top bar must live in bit 0");
  }
}

void test_bit7_is_the_bottom_row(void) {
  // '_' is a solid bar on row 7 and nothing else.
  const LoveGlyph &underscore = loveFontGlyph('_');
  const uint8_t *cols = loveFontColumns(underscore);
  for (uint8_t i = 0; i < underscore.width; i++) {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0x80, cols[i], "'_' must be bit 7 only -- bit order is wrong");
  }
}

// --- Polish -----------------------------------------------------------------

void test_all_polish_letters_exist(void) {
  for (size_t i = 0; i < kPolishCount; i++) {
    const LoveGlyph *g = loveFontFind(kPolish[i]);
    TEST_ASSERT_NOT_NULL_MESSAGE(g, "a Polish letter is missing from the font");
  }
}

void test_no_polish_letter_is_blank(void) {
  for (size_t i = 0; i < kPolishCount; i++) {
    const LoveGlyph &g = loveFontGlyph(kPolish[i]);
    const uint8_t *cols = loveFontColumns(g);
    bool any = false;
    for (uint8_t c = 0; c < g.width; c++) any |= cols[c] != 0;
    TEST_ASSERT_TRUE_MESSAGE(any, "a Polish letter renders as blank columns");
  }
}

void test_accented_letters_differ_from_their_base(void) {
  for (size_t i = 0; i < kAccentPairCount; i++) {
    const LoveGlyph &accented = loveFontGlyph(kAccentPairs[i][0]);
    const LoveGlyph &base = loveFontGlyph(kAccentPairs[i][1]);
    TEST_ASSERT_FALSE_MESSAGE(
        sameGlyph(accented, base),
        "accented letter is identical to its base -- the mark is missing");
  }
}

void test_ogonek_hangs_below_the_baseline(void) {
  // a-ogonek and e-ogonek are the only lowercase letters using row 7 for a
  // mark rather than a descender.
  const uint32_t ogoneks[] = {0x0104, 0x0105, 0x0118, 0x0119};
  for (uint32_t cp : ogoneks) {
    const LoveGlyph &g = loveFontGlyph(cp);
    const uint8_t *cols = loveFontColumns(g);
    bool row7 = false;
    for (uint8_t c = 0; c < g.width; c++) row7 |= (cols[c] & 0x80) != 0;
    TEST_ASSERT_TRUE_MESSAGE(row7, "ogonek must occupy row 7");
  }
}

void test_heart_exists_and_is_wide(void) {
  const LoveGlyph *heart = loveFontFind(0x2665);
  TEST_ASSERT_NOT_NULL_MESSAGE(heart, "the heart glyph is the whole point");
  TEST_ASSERT_EQUAL_UINT8(7, heart->width);
}

int main(int, char **) {
  UNITY_BEGIN();

  RUN_TEST(test_finds_ascii_glyph);
  RUN_TEST(test_missing_codepoint_returns_null);
  RUN_TEST(test_missing_codepoint_falls_back_to_notdef);
  RUN_TEST(test_binary_search_reaches_both_ends);
  RUN_TEST(test_table_is_sorted);
  RUN_TEST(test_every_glyph_stays_inside_the_column_array);

  RUN_TEST(test_bit0_is_the_top_row);
  RUN_TEST(test_bit7_is_the_bottom_row);

  RUN_TEST(test_all_polish_letters_exist);
  RUN_TEST(test_no_polish_letter_is_blank);
  RUN_TEST(test_accented_letters_differ_from_their_base);
  RUN_TEST(test_ogonek_hangs_below_the_baseline);
  RUN_TEST(test_heart_exists_and_is_wide);

  return UNITY_END();
}
