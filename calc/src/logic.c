/*
 * logic.c — ZACH GPT v5.0: platform-independent UI logic.
 * See logic.h. Unit-tested on the host by calc/test/test_logic.c.
 */
#include <string.h>
#include "logic.h"

/* ── T9 ─────────────────────────────────────────────────────────────── */
/* Indexed by digit. Letter groups follow the physical TI-84 keypad
 * (7 8 9 on the top row, 1 2 3 on the bottom), so the alphabet reads
 * top-down: 7 = punctuation, 8 = ABC, 9 = DEF ... 1 = PQRS, 2 = TUV, 3 = WXYZ. */
static const char *const T9_ABC[10] = {
    " ", "PQRS", "TUV", "WXYZ", "GHI", "JKL", "MNO", ".,?!'", "ABC", "DEF"
};
/* 123 mode: first tap is the digit, second tap its symbol. */
static const char *const T9_NUM[10] = {
    "0/", "1%", "2$", "3.", "4(", "5)", "6,", "7+", "8-", "9*"
};

void t9_init(t9_t *s) {
    memset(s, 0, sizeof *s);
    s->pend_key = -1;
}

const char *t9_letters(const t9_t *s, uint8_t key) {
    if (key > 9) return "";
    return s->num_mode ? T9_NUM[key] : T9_ABC[key];
}

char t9_pending_char(const t9_t *s) {
    if (s->pend_key < 0) return 0;
    return t9_letters(s, (uint8_t)s->pend_key)[s->pend_idx];
}

static void t9_append(t9_t *s, char c) {
    if (s->len < MAX_INPUT - 1) {
        s->text[s->len++] = c;
        s->text[s->len] = '\0';
    }
}

void t9_commit(t9_t *s) {
    char c = t9_pending_char(s);
    s->pend_key = -1;
    s->pend_idx = 0;
    if (c) t9_append(s, c);
}

void t9_digit(t9_t *s, uint8_t key, uint32_t now_ms) {
    const char *ls;
    uint8_t n;
    if (key > 9) return;
    ls = t9_letters(s, key);
    n = (uint8_t)strlen(ls);
    if (s->pend_key == (int8_t)key) {              /* same key: cycle, restart timer */
        s->pend_idx = (uint8_t)((s->pend_idx + 1) % n);
        s->pend_t0 = now_ms;
        return;
    }
    if (s->pend_key >= 0) t9_commit(s);             /* other key: commit what was pending */
    if (n == 1) {                                   /* single-char key (space): no cycling */
        t9_append(s, ls[0]);
        return;
    }
    s->pend_key = (int8_t)key;
    s->pend_idx = 0;
    s->pend_t0 = now_ms;
}

void t9_tick(t9_t *s, uint32_t now_ms) {
    if (s->pend_key >= 0 && (uint32_t)(now_ms - s->pend_t0) >= T9_COMMIT_MS)
        t9_commit(s);
}

void t9_alpha(t9_t *s) {
    if (s->pend_key >= 0) t9_commit(s);
    s->num_mode = (uint8_t)!s->num_mode;
}

void t9_del(t9_t *s) {
    if (s->pend_key >= 0) {                         /* drop the pending letter only */
        s->pend_key = -1;
        s->pend_idx = 0;
        return;
    }
    if (s->len) {
        s->len--;
        s->text[s->len] = '\0';
    }
}

bool t9_enter(t9_t *s) {
    uint16_t a = 0, b, n;
    if (s->pend_key >= 0) t9_commit(s);
    b = s->len;
    while (a < b && s->text[a] == ' ') a++;
    while (b > a && s->text[b - 1] == ' ') b--;
    n = (uint16_t)(b - a);
    if (a && n) memmove(s->text, s->text + a, n);
    s->len = n;
    s->text[n] = '\0';
    return n > 0;
}

void t9_set_text(t9_t *s, const char *text) {
    size_t n = strlen(text);
    if (n > MAX_INPUT - 1) n = MAX_INPUT - 1;
    memcpy(s->text, text, n);
    s->text[n] = '\0';
    s->len = (uint16_t)n;
    s->pend_key = -1;
    s->pend_idx = 0;
}

/* ── Word wrap ──────────────────────────────────────────────────────── */
void wrap_text(const char *text, uint16_t len, uint8_t width, uint8_t cursor_cols, wrap_t *out) {
    uint16_t pos = 0;
    uint8_t n = 0;
    if (width == 0) width = 1;
    while (pos < len && n < WRAP_MAX_LINES) {
        uint16_t remaining = (uint16_t)(len - pos);
        if (remaining <= width) {                   /* everything left fits */
            out->start[n] = pos;
            out->len[n] = (uint8_t)remaining;
            n++;
            pos = len;
            break;
        }
        {
            uint16_t i, cut = 0;
            bool found = false;
            for (i = pos; i <= (uint16_t)(pos + width); i++)   /* pos+width < len here */
                if (text[i] == ' ') { cut = i; found = true; }
            out->start[n] = pos;
            if (!found) {                           /* word longer than the line: hard split */
                out->len[n] = width;
                pos = (uint16_t)(pos + width);
            } else {                                /* break at the last space, consume it */
                out->len[n] = (uint8_t)(cut - pos);
                pos = (uint16_t)(cut + 1);
            }
            n++;
        }
    }
    if (n == 0) {
        out->start[0] = 0;
        out->len[0] = 0;
        n = 1;
    }
    out->count = n;
    out->cursor_line = (uint8_t)(n - 1);
    out->cursor_col = out->len[n - 1];
    if ((uint16_t)out->cursor_col + cursor_cols > width) {   /* cursor wraps to a new line */
        out->cursor_line = n;
        out->cursor_col = 0;
    }
}

/* ── Chat history ───────────────────────────────────────────────────── */
void chat_init(chat_t *c) {
    c->count = 0;
    c->head = 0;
}

void chat_push(chat_t *c, uint8_t role, const char *text) {
    uint8_t idx;
    msg_t *m;
    size_t n;
    if (c->count < MAX_MSGS) {
        idx = (uint8_t)((c->head + c->count) % MAX_MSGS);
        c->count++;
    } else {                                        /* full: overwrite the oldest */
        idx = c->head;
        c->head = (uint8_t)((c->head + 1) % MAX_MSGS);
    }
    m = &c->m[idx];
    m->role = role;
    n = strlen(text);
    if (n > MAX_RESP - 1) n = MAX_RESP - 1;
    memcpy(m->text, text, n);
    m->text[n] = '\0';
}

const msg_t *chat_get(const chat_t *c, uint8_t i) {
    if (i >= c->count) return 0;
    return &c->m[(c->head + i) % MAX_MSGS];
}

bool chat_pop_last(chat_t *c, char *out, uint16_t out_sz) {
    const msg_t *m;
    if (c->count == 0) return false;
    m = chat_get(c, (uint8_t)(c->count - 1));
    if (out && out_sz) {
        size_t n = strlen(m->text);
        if (n > (size_t)(out_sz - 1)) n = out_sz - 1;
        memcpy(out, m->text, n);
        out[n] = '\0';
    }
    c->count--;
    return true;
}

/* ── Chat layout ────────────────────────────────────────────────────── */
static wrap_t layout_wrap;      /* static: the CE stack is only ~4 KiB */

uint16_t chat_layout(const chat_t *c, bool thinking, bubble_t *out, uint8_t *n_out) {
    uint16_t y = 0;
    uint8_t n = 0, i;
    for (i = 0; i < c->count; i++) {
        const msg_t *m = chat_get(c, i);
        bubble_t *b = &out[n++];
        uint8_t cols = (m->role == ROLE_USER) ? USER_COLS : AI_COLS;
        uint8_t l, maxlen = 0;
        wrap_text(m->text, (uint16_t)strlen(m->text), cols, 0, &layout_wrap);
        for (l = 0; l < layout_wrap.count; l++)
            if (layout_wrap.len[l] > maxlen) maxlen = layout_wrap.len[l];
        b->lines = layout_wrap.count;
        b->cols = cols;
        b->role = m->role;
        b->w = (uint16_t)(maxlen * 8 + 12);
        b->h = (uint16_t)(b->lines * 10 + 12);
        b->x = (m->role == ROLE_USER) ? (uint16_t)(CHAT_RIGHT_X - b->w) : CHAT_LEFT_X;
        b->y = y;
        y = (uint16_t)(y + b->h + BUBBLE_GAP);
    }
    if (thinking) {
        bubble_t *b = &out[n++];
        b->x = CHAT_LEFT_X;
        b->y = y;
        b->w = PILL_W;
        b->h = PILL_H;
        b->lines = 1;
        b->cols = 0;
        b->role = ROLE_AI;
        y = (uint16_t)(y + PILL_H + BUBBLE_GAP);
    }
    *n_out = n;
    return n ? (uint16_t)(y - BUBBLE_GAP) : 0;
}

/* ── Scroll ─────────────────────────────────────────────────────────── */
uint16_t scroll_max(uint16_t content_h, uint16_t view_h) {
    return content_h > view_h ? (uint16_t)(content_h - view_h) : 0;
}

void scroll_thumb(uint16_t content_h, uint16_t view_h, uint16_t scroll,
                  uint8_t track_y, uint8_t *thumb_y, uint8_t *thumb_h) {
    uint32_t h, yoff;
    uint16_t maxs;
    if (content_h <= view_h) {
        *thumb_y = track_y;
        *thumb_h = (uint8_t)view_h;
        return;
    }
    h = (uint32_t)view_h * view_h / content_h;
    if (h < 12) h = 12;
    maxs = (uint16_t)(content_h - view_h);
    if (scroll > maxs) scroll = maxs;
    yoff = (uint32_t)(view_h - h) * scroll / maxs;
    *thumb_h = (uint8_t)h;
    *thumb_y = (uint8_t)(track_y + yoff);
}

/* ── Splash rain ────────────────────────────────────────────────────── */
static const char RAIN_SET[] = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ+-=/<>%#$";
#define RAIN_SET_N ((uint8_t)(sizeof RAIN_SET - 1))   /* 43 chars */

static uint32_t lcg32(uint32_t *s) {
    *s = *s * 1103515245u + 12345u;
    return (*s >> 16) & 0x7fffu;
}

void rain_init(rain_col_t *cols, uint32_t master_seed) {
    uint32_t s = master_seed;
    uint8_t i;
    lcg32(&s);                                      /* discard the low-entropy first draw */
    for (i = 0; i < RAIN_COLS; i++) {
        rain_col_t *c = &cols[i];
        c->seed   = (uint16_t)(lcg32(&s) | 1u);
        c->period = (uint8_t)(1 + lcg32(&s) % 3);   /* 100 / 200 / 300 ms per 8-px step */
        c->phase  = (uint8_t)(lcg32(&s) % c->period);
        c->pos    = (uint8_t)(lcg32(&s) % RAIN_LEN);
        c->dim    = (uint8_t)((lcg32(&s) % 100) < 45);
        c->moved  = 0;
    }
}

void rain_step(rain_col_t *cols, uint8_t tick) {
    uint8_t i;
    for (i = 0; i < RAIN_COLS; i++) {
        rain_col_t *c = &cols[i];
        c->moved = (uint8_t)((uint8_t)(tick + c->phase) % c->period == 0);
        if (c->moved)
            c->pos = (uint8_t)((c->pos + 1) % RAIN_LEN);
    }
}

void rain_chars(const rain_col_t *c, char *out) {
    uint16_t s = c->seed;
    uint8_t j;
    for (j = 0; j < RAIN_LEN; j++) {
        s = (uint16_t)(s * 25173u + 13849u);        /* 16-bit LCG; identical on eZ80 and host */
        out[j] = RAIN_SET[(s >> 4) % RAIN_SET_N];
    }
}
