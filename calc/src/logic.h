/*
 * logic.h — ZACH GPT v5.0: platform-independent UI logic.
 * No graphx/keypadc/usb includes; fixed-width types only so the same code
 * behaves identically on the eZ80 (24-bit int) and on a host compiler (tests).
 */
#ifndef LOGIC_H
#define LOGIC_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_INPUT   512
#define MAX_RESP    512

/* ── T9 multi-tap input ─────────────────────────────────────────────── */
#define T9_COMMIT_MS   900

typedef struct {
    char     text[MAX_INPUT];
    uint16_t len;
    uint8_t  num_mode;   /* 0 = ABC, 1 = 123 */
    int8_t   pend_key;   /* -1 = nothing pending, else 0..9 */
    uint8_t  pend_idx;   /* index into t9_letters(pend_key) */
    uint32_t pend_t0;    /* ms timestamp of the last tap */
} t9_t;

void        t9_init(t9_t *s);
const char *t9_letters(const t9_t *s, uint8_t key);   /* letters of a key in the current mode */
char        t9_pending_char(const t9_t *s);            /* 0 when nothing is pending */
void        t9_digit(t9_t *s, uint8_t key, uint32_t now_ms);
void        t9_tick(t9_t *s, uint32_t now_ms);         /* commits when the 900 ms timer expires */
void        t9_alpha(t9_t *s);                          /* commit + toggle ABC/123 */
void        t9_del(t9_t *s);                            /* drop pending, else delete last char */
bool        t9_enter(t9_t *s);                          /* commit + trim; false if empty */
void        t9_commit(t9_t *s);
void        t9_set_text(t9_t *s, const char *text);     /* restore text, clear pending */

/* ── Word wrap ──────────────────────────────────────────────────────── */
#define WRAP_MAX_LINES 64

typedef struct {
    uint16_t start[WRAP_MAX_LINES];
    uint8_t  len[WRAP_MAX_LINES];
    uint8_t  count;        /* number of text lines, always >= 1 */
    uint8_t  cursor_line;  /* may equal count: cursor on a new, empty line */
    uint8_t  cursor_col;
} wrap_t;

/* Greedy word wrap at `width` columns; words longer than width are hard-split.
 * A cursor of `cursor_cols` columns is placed after the text; if it does not
 * fit on the last line it moves to a new line (cursor_line == count). */
void wrap_text(const char *text, uint16_t len, uint8_t width, uint8_t cursor_cols, wrap_t *out);

/* ── Chat history + layout ──────────────────────────────────────────── */
#define MAX_MSGS      6
#define ROLE_USER     0
#define ROLE_AI       1
#define CHAT_VIEW_H   196
#define USER_COLS     29       /* (248 - 12) / 8 */
#define AI_COLS       32       /* (272 - 12) / 8 */
#define BUBBLE_GAP    8
#define CHAT_LEFT_X   6
#define CHAT_RIGHT_X  300
#define PILL_W        116
#define PILL_H        22

typedef struct { uint8_t role; char text[MAX_RESP]; } msg_t;
typedef struct { msg_t m[MAX_MSGS]; uint8_t count; uint8_t head; } chat_t;

void          chat_init(chat_t *c);
void          chat_push(chat_t *c, uint8_t role, const char *text);
const msg_t  *chat_get(const chat_t *c, uint8_t i);              /* i = 0 is the oldest */
bool          chat_pop_last(chat_t *c, char *out, uint16_t out_sz);

typedef struct { uint16_t x, y, w, h; uint8_t lines; uint8_t cols; uint8_t role; } bubble_t;

/* Lays out every message (plus the THINKING pill when `thinking`) from y = 0.
 * Returns the total content height. `out` needs MAX_MSGS + 1 entries. */
uint16_t chat_layout(const chat_t *c, bool thinking, bubble_t *out, uint8_t *n_out);

/* ── Scroll ─────────────────────────────────────────────────────────── */
uint16_t scroll_max(uint16_t content_h, uint16_t view_h);
void     scroll_thumb(uint16_t content_h, uint16_t view_h, uint16_t scroll,
                      uint8_t track_y, uint8_t *thumb_y, uint8_t *thumb_h);

/* ── Splash rain ────────────────────────────────────────────────────── */
#ifndef RAIN_COLS
#define RAIN_COLS 40
#endif
#define RAIN_LEN  31

typedef struct {
    uint16_t seed;     /* per-column LCG seed: regenerates the 31 chars */
    uint8_t  pos;      /* row offset of string index 0 (the head) */
    uint8_t  period;   /* advance every `period` ticks of 100 ms (1..3) */
    uint8_t  phase;    /* random phase 0..period-1 */
    uint8_t  dim;      /* 1 = RAIN_DIM body / RAIN head, 0 = RAIN body / RAIN_HEAD head */
} rain_col_t;

void rain_init(rain_col_t *cols, uint32_t master_seed);
void rain_step(rain_col_t *cols, uint8_t tick);
void rain_chars(const rain_col_t *c, char *out);   /* out[RAIN_LEN], not NUL-terminated */

#endif /* LOGIC_H */
