/* Host unit tests for calc/src/logic.c — build with gcc, not part of the CE build. */
#include <stdio.h>
#include <string.h>
#include "logic.h"

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b)) != 0) { fails++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

/* ── T9 ───────────────────────────────────────────────────────────── */
static void test_t9_init(void) {
    t9_t s; t9_init(&s);
    CHECK(s.len == 0); CHECK_STR(s.text, ""); CHECK(s.pend_key == -1); CHECK(s.num_mode == 0);
    CHECK(t9_pending_char(&s) == 0);
}
static void test_t9_tables_follow_physical_keypad(void) {
    t9_t s; t9_init(&s);
    CHECK_STR(t9_letters(&s, 7), ".,?!'"); CHECK_STR(t9_letters(&s, 8), "ABC"); CHECK_STR(t9_letters(&s, 9), "DEF");
    CHECK_STR(t9_letters(&s, 4), "GHI");   CHECK_STR(t9_letters(&s, 5), "JKL"); CHECK_STR(t9_letters(&s, 6), "MNO");
    CHECK_STR(t9_letters(&s, 1), "PQRS");  CHECK_STR(t9_letters(&s, 2), "TUV"); CHECK_STR(t9_letters(&s, 3), "WXYZ");
    CHECK_STR(t9_letters(&s, 0), " ");
    s.num_mode = 1;
    CHECK_STR(t9_letters(&s, 1), "1%"); CHECK_STR(t9_letters(&s, 0), "0/"); CHECK_STR(t9_letters(&s, 7), "7+");
}
static void test_t9_first_tap_pends_without_typing(void) {
    t9_t s; t9_init(&s);
    t9_digit(&s, 8, 1000);
    CHECK(s.len == 0); CHECK(s.pend_key == 8); CHECK(t9_pending_char(&s) == 'A'); CHECK(s.pend_t0 == 1000);
}
static void test_t9_repeat_taps_cycle_and_wrap(void) {
    t9_t s; t9_init(&s);
    t9_digit(&s, 8, 0); t9_digit(&s, 8, 100); t9_digit(&s, 8, 200);
    CHECK(t9_pending_char(&s) == 'C'); CHECK(s.pend_t0 == 200);
    t9_digit(&s, 8, 300);
    CHECK(t9_pending_char(&s) == 'A'); CHECK(s.len == 0);
}
static void test_t9_timer_commits_after_900ms_from_last_tap(void) {
    t9_t s; t9_init(&s);
    t9_digit(&s, 8, 0); t9_digit(&s, 8, 800);
    t9_tick(&s, 1600); CHECK(s.len == 0); CHECK(s.pend_key == 8);     /* restarted at 800 → expires 1700 */
    t9_tick(&s, 1699); CHECK(s.len == 0);
    t9_tick(&s, 1700); CHECK_STR(s.text, "B"); CHECK(s.pend_key == -1); CHECK(t9_pending_char(&s) == 0);
}
static void test_t9_other_key_commits_pending(void) {
    t9_t s; t9_init(&s);
    t9_digit(&s, 8, 0); t9_digit(&s, 9, 100);
    CHECK_STR(s.text, "A"); CHECK(t9_pending_char(&s) == 'D');
}
static void test_t9_single_char_key_commits_immediately(void) {
    t9_t s; t9_init(&s);
    t9_digit(&s, 8, 0); t9_digit(&s, 0, 100);
    CHECK_STR(s.text, "A "); CHECK(s.pend_key == -1);
}
static void test_t9_alpha_commits_and_toggles_mode(void) {
    t9_t s; t9_init(&s);
    t9_digit(&s, 8, 0); t9_alpha(&s);
    CHECK_STR(s.text, "A"); CHECK(s.pend_key == -1); CHECK(s.num_mode == 1);
    t9_alpha(&s); CHECK(s.num_mode == 0);
}
static void test_t9_types_12_percent(void) {
    t9_t s; t9_init(&s);
    t9_alpha(&s);
    t9_digit(&s, 1, 0); t9_digit(&s, 2, 100); t9_digit(&s, 1, 200); t9_digit(&s, 1, 300);
    CHECK_STR(s.text, "12"); CHECK(t9_pending_char(&s) == '%');
    t9_tick(&s, 1200);
    CHECK_STR(s.text, "12%");
}
static void test_t9_del_drops_pending_then_deletes(void) {
    t9_t s; t9_init(&s);
    t9_digit(&s, 8, 0); t9_digit(&s, 9, 100);          /* "A" + pending D */
    t9_del(&s); CHECK_STR(s.text, "A"); CHECK(s.pend_key == -1);
    t9_del(&s); CHECK_STR(s.text, ""); CHECK(s.len == 0);
    t9_del(&s); CHECK(s.len == 0);                        /* no underflow */
}
static void test_t9_del_cancels_timer(void) {
    t9_t s; t9_init(&s);
    t9_digit(&s, 8, 0); t9_del(&s); t9_tick(&s, 5000);
    CHECK_STR(s.text, "");
}
static void test_t9_enter_commits_and_trims(void) {
    t9_t s; t9_init(&s);
    t9_set_text(&s, "  HI THERE  ");
    CHECK(t9_enter(&s)); CHECK_STR(s.text, "HI THERE"); CHECK(s.len == 8);
    t9_init(&s); t9_digit(&s, 8, 0);
    CHECK(t9_enter(&s)); CHECK_STR(s.text, "A");
}
static void test_t9_enter_rejects_empty(void) {
    t9_t s; t9_init(&s);
    CHECK(!t9_enter(&s));
    t9_set_text(&s, "   "); CHECK(!t9_enter(&s)); CHECK(s.len == 0);
}
static void test_t9_respects_max_input(void) {
    t9_t s; t9_init(&s);
    char big[MAX_INPUT + 10]; memset(big, 'A', sizeof big); big[MAX_INPUT - 1] = '\0';
    t9_set_text(&s, big);
    CHECK(s.len == MAX_INPUT - 1);
    t9_digit(&s, 8, 0); t9_tick(&s, 1000);
    CHECK(s.len == MAX_INPUT - 1); CHECK(s.text[MAX_INPUT - 1] == '\0');
}
static void test_t9_set_text_clears_pending(void) {
    t9_t s; t9_init(&s);
    t9_digit(&s, 8, 0); t9_set_text(&s, "RESTORED");
    CHECK_STR(s.text, "RESTORED"); CHECK(s.len == 8); CHECK(s.pend_key == -1);
}

/* ── Wrap ─────────────────────────────────────────────────────────── */
static void test_wrap_empty_text(void) {
    wrap_t w; wrap_text("", 0, 37, 1, &w);
    CHECK(w.count == 1); CHECK(w.len[0] == 0); CHECK(w.cursor_line == 0); CHECK(w.cursor_col == 0);
}
static void test_wrap_single_line(void) {
    wrap_t w; const char *t = "HELLO WORLD"; wrap_text(t, 11, 37, 1, &w);
    CHECK(w.count == 1); CHECK(w.start[0] == 0); CHECK(w.len[0] == 11);
    CHECK(w.cursor_line == 0); CHECK(w.cursor_col == 11);
}
static void test_wrap_breaks_at_space(void) {
    wrap_t w; wrap_text("AAAA BBBB", 9, 6, 1, &w);
    CHECK(w.count == 2); CHECK(w.start[0] == 0); CHECK(w.len[0] == 4);
    CHECK(w.start[1] == 5); CHECK(w.len[1] == 4);
}
static void test_wrap_hard_splits_long_word(void) {
    wrap_t w; wrap_text("ABCDEFGHIJ", 10, 4, 1, &w);
    CHECK(w.count == 3); CHECK(w.len[0] == 4); CHECK(w.len[1] == 4); CHECK(w.start[2] == 8); CHECK(w.len[2] == 2);
}
static void test_wrap_space_at_width_is_consumed(void) {
    wrap_t w; wrap_text("ABCD EFG", 8, 4, 1, &w);
    CHECK(w.count == 2); CHECK(w.len[0] == 4); CHECK(w.start[1] == 5); CHECK(w.len[1] == 3);
}
static void test_wrap_cursor_moves_to_new_line_when_full(void) {
    char t[38]; memset(t, 'X', 37); t[37] = '\0';
    wrap_t w; wrap_text(t, 37, 37, 1, &w);
    CHECK(w.count == 1); CHECK(w.len[0] == 37); CHECK(w.cursor_line == 1); CHECK(w.cursor_col == 0);
}
static void test_wrap_two_col_cursor(void) {
    char t[38]; memset(t, 'X', 37); t[36] = '\0';
    wrap_t w; wrap_text(t, 36, 37, 2, &w);           /* 36 + 2 > 37 → new line */
    CHECK(w.cursor_line == 1); CHECK(w.cursor_col == 0);
    t[35] = '\0'; wrap_text(t, 35, 37, 2, &w);       /* 35 + 2 == 37 → fits */
    CHECK(w.cursor_line == 0); CHECK(w.cursor_col == 35);
}
static void test_wrap_keeps_multiple_spaces(void) {
    wrap_t w; wrap_text("A  B", 4, 37, 1, &w);
    CHECK(w.count == 1); CHECK(w.len[0] == 4); CHECK(w.cursor_col == 4);
}
static void test_wrap_many_lines_stays_in_bounds(void) {
    char t[MAX_RESP]; for (int i = 0; i < MAX_RESP - 1; i++) t[i] = (i % 16 == 15) ? ' ' : 'Q'; t[MAX_RESP - 1] = '\0';
    wrap_t w; wrap_text(t, MAX_RESP - 1, 29, 1, &w);
    CHECK(w.count > 1); CHECK(w.count <= WRAP_MAX_LINES);
    for (int i = 0; i < w.count; i++) CHECK(w.len[i] <= 29);
}

/* ── Chat ─────────────────────────────────────────────────────────── */
static void test_chat_ring_drops_oldest(void) {
    chat_t c; chat_init(&c);
    char buf[8];
    for (int i = 0; i < 7; i++) { snprintf(buf, sizeof buf, "M%d", i); chat_push(&c, i & 1, buf); }
    CHECK(c.count == MAX_MSGS);
    CHECK_STR(chat_get(&c, 0)->text, "M1"); CHECK_STR(chat_get(&c, 5)->text, "M6");
    CHECK(chat_get(&c, 0)->role == 1);
}
static void test_chat_pop_last(void) {
    chat_t c; chat_init(&c); char out[16];
    chat_push(&c, ROLE_USER, "FIRST"); chat_push(&c, ROLE_USER, "SECOND");
    CHECK(chat_pop_last(&c, out, sizeof out)); CHECK_STR(out, "SECOND"); CHECK(c.count == 1);
    CHECK(chat_pop_last(&c, out, sizeof out)); CHECK(c.count == 0);
    CHECK(!chat_pop_last(&c, out, sizeof out));
}
static void test_chat_layout_single_user_bubble(void) {
    chat_t c; chat_init(&c); bubble_t b[MAX_MSGS + 1]; uint8_t n;
    chat_push(&c, ROLE_USER, "HI");
    uint16_t h = chat_layout(&c, false, b, &n);
    CHECK(n == 1); CHECK(b[0].w == 28); CHECK(b[0].h == 22); CHECK(b[0].x == 272); CHECK(b[0].y == 0);
    CHECK(b[0].lines == 1); CHECK(b[0].cols == USER_COLS); CHECK(b[0].role == ROLE_USER); CHECK(h == 22);
}
static void test_chat_layout_stacks_with_gap_and_thinking_pill(void) {
    chat_t c; chat_init(&c); bubble_t b[MAX_MSGS + 1]; uint8_t n;
    chat_push(&c, ROLE_USER, "HI"); chat_push(&c, ROLE_AI, "HELLO");
    uint16_t h = chat_layout(&c, true, b, &n);
    CHECK(n == 3);
    CHECK(b[1].y == 30); CHECK(b[1].x == 6); CHECK(b[1].w == 52); CHECK(b[1].cols == AI_COLS);
    CHECK(b[2].y == 60); CHECK(b[2].w == PILL_W); CHECK(b[2].h == PILL_H); CHECK(b[2].x == 6);
    CHECK(h == 82);
}
static void test_chat_layout_wraps_ai_at_32_and_user_at_29(void) {
    chat_t c; chat_init(&c); bubble_t b[MAX_MSGS + 1]; uint8_t n;
    char t[70]; memset(t, 'W', 64); t[64] = '\0';            /* 64 chars, no spaces */
    chat_push(&c, ROLE_AI, t); chat_push(&c, ROLE_USER, t);
    chat_layout(&c, false, b, &n);
    CHECK(b[0].lines == 2); CHECK(b[0].w == 32 * 8 + 12); CHECK(b[0].h == 32);
    CHECK(b[1].lines == 3); CHECK(b[1].w == 29 * 8 + 12); CHECK(b[1].h == 42); CHECK(b[1].x == 300 - (29 * 8 + 12));
}

/* ── Scroll ───────────────────────────────────────────────────────── */
static void test_scroll_max(void) {
    CHECK(scroll_max(100, 196) == 0); CHECK(scroll_max(196, 196) == 0); CHECK(scroll_max(300, 196) == 104);
}
static void test_scroll_thumb_fills_track_when_content_fits(void) {
    uint8_t y, h; scroll_thumb(100, 196, 0, 28, &y, &h);
    CHECK(y == 28); CHECK(h == 196);
}
static void test_scroll_thumb_proportional(void) {
    uint8_t y, h;
    scroll_thumb(392, 196, 0, 28, &y, &h);   CHECK(h == 98); CHECK(y == 28);
    scroll_thumb(392, 196, 196, 28, &y, &h); CHECK(h == 98); CHECK(y == 126);
    scroll_thumb(392, 196, 98, 28, &y, &h);  CHECK(y == 77);
    scroll_thumb(10000, 196, 0, 28, &y, &h); CHECK(h == 12);
}

/* ── Rain ─────────────────────────────────────────────────────────── */
static void test_rain_init_is_deterministic(void) {
    rain_col_t a[RAIN_COLS], b[RAIN_COLS];
    rain_init(a, 7); rain_init(b, 7);
    CHECK(memcmp(a, b, sizeof a) == 0);
    int dim = 0, differ = 0;
    for (int i = 0; i < RAIN_COLS; i++) {
        CHECK(a[i].period >= 1 && a[i].period <= 3); CHECK(a[i].phase < a[i].period); CHECK(a[i].pos < RAIN_LEN);
        dim += a[i].dim; if (i && a[i].seed != a[i - 1].seed) differ++;
    }
    CHECK(dim >= 8 && dim <= 28); CHECK(differ > RAIN_COLS / 2);
}
static void test_rain_chars_from_charset(void) {
    static const char set[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ+-=/<>%#$";
    rain_col_t a[RAIN_COLS]; rain_init(a, 7);
    char s1[RAIN_LEN], s2[RAIN_LEN], s3[RAIN_LEN];
    rain_chars(&a[0], s1); rain_chars(&a[0], s2); rain_chars(&a[1], s3);
    CHECK(memcmp(s1, s2, RAIN_LEN) == 0); CHECK(memcmp(s1, s3, RAIN_LEN) != 0);
    for (int j = 0; j < RAIN_LEN; j++) CHECK(s1[j] != '\0' && strchr(set, s1[j]) != NULL);
}
static void test_rain_step_advances_by_period(void) {
    rain_col_t a[RAIN_COLS]; rain_init(a, 7);
    a[0].period = 1; a[0].phase = 0; a[0].pos = 30;
    a[1].period = 3; a[1].phase = 0; a[1].pos = 0;
    rain_step(a, 0); CHECK(a[0].pos == 0); CHECK(a[1].pos == 1);
    rain_step(a, 1); CHECK(a[0].pos == 1); CHECK(a[1].pos == 1);
    rain_step(a, 2); CHECK(a[1].pos == 1);
    rain_step(a, 3); CHECK(a[0].pos == 3); CHECK(a[1].pos == 2);
}

int main(void) {
    test_t9_init(); test_t9_tables_follow_physical_keypad(); test_t9_first_tap_pends_without_typing();
    test_t9_repeat_taps_cycle_and_wrap(); test_t9_timer_commits_after_900ms_from_last_tap();
    test_t9_other_key_commits_pending(); test_t9_single_char_key_commits_immediately();
    test_t9_alpha_commits_and_toggles_mode(); test_t9_types_12_percent(); test_t9_del_drops_pending_then_deletes();
    test_t9_del_cancels_timer(); test_t9_enter_commits_and_trims(); test_t9_enter_rejects_empty();
    test_t9_respects_max_input(); test_t9_set_text_clears_pending();
    test_wrap_empty_text(); test_wrap_single_line(); test_wrap_breaks_at_space(); test_wrap_hard_splits_long_word();
    test_wrap_space_at_width_is_consumed(); test_wrap_cursor_moves_to_new_line_when_full(); test_wrap_two_col_cursor();
    test_wrap_keeps_multiple_spaces(); test_wrap_many_lines_stays_in_bounds();
    test_chat_ring_drops_oldest(); test_chat_pop_last(); test_chat_layout_single_user_bubble();
    test_chat_layout_stacks_with_gap_and_thinking_pill(); test_chat_layout_wraps_ai_at_32_and_user_at_29();
    test_scroll_max(); test_scroll_thumb_fills_track_when_content_fits(); test_scroll_thumb_proportional();
    test_rain_init_is_deterministic(); test_rain_chars_from_charset(); test_rain_step_advances_by_period();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
