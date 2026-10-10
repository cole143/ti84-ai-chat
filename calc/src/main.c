/*
 * ZACH GPT v5.0 — TI-84 Plus CE AI chat over USB serial.
 * Screens: Splash (character rain, waits for USB), T9 Typing on the physical
 * 0-9 keys, Thinking, Answer (scrollable chat bubbles).
 * Libraries: graphx, keypadc, usbdrvce, srldrvce. Protocol: "question\n" out,
 * "answer\n" back (bridge.py on the PC). UI logic lives in logic.c.
 *
 * Drawing strategy: the splash is drawn straight to the screen and only the
 * rain columns that moved are repainted each 100 ms tick; the other screens are
 * drawn as full frames into the back buffer and shown with gfx_SwapDraw().
 * usb_HandleEvents() is also called inside every drawing loop so USB enumeration
 * and serial traffic are serviced even in the middle of a frame (a splash frame
 * measured 289 ms on hardware when drawn naively, which starved USB and made
 * Windows fail the descriptor request).
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <graphx.h>
#include <keypadc.h>
#include <srldrvce.h>
#include <usbdrvce.h>

#include "logic.h"

#ifdef ZG_DEBUG
#include <stdio.h>
/* Diagnostic overlay on the splash hint line (build with make CFLAGS="... -DZG_DEBUG"):
 * T seconds since start (from clock), E usb_HandleEvents calls in the last second,
 * X last non-zero usb_HandleEvents error, F ms spent drawing the last frame/tick,
 * R usb_GetRole(), V last 6 USB event codes seen by the handler (newest last). */
static uint32_t dbg_now, dbg_ev_count, dbg_ev_rate, dbg_ev_t0;
static uint8_t  dbg_last_err, dbg_events[6], dbg_nevents;
static uint16_t dbg_frame_ms;
static void dbg_note_event(uint8_t ev) {
    if (dbg_nevents < 6) { dbg_events[dbg_nevents++] = ev; return; }
    memmove(dbg_events, dbg_events + 1, 5);
    dbg_events[5] = ev;
}
#endif

/* ── Config ─────────────────────────────────────────────────────────── */
#define SRL_BUF_SZ  2048        /* srldrvce splits it: 1 KB rx + 1 KB tx; a message is <= 513 B */

#define SCREEN_W    320
#define SCREEN_H    240
#define FONT_H      8
#define CHAR_W      8

#define INPUT_COLS      37
#define KEY_SAMPLE_MS   10
#define KEY_LOCKOUT_MS  30
#define FLASH_MS        140
#define ANIM_TICK_MS    100     /* rain step; dots = 4 ticks, spinner = 3, caret = 5 */

/* ── Palette (README table, indices 0-14) ───────────────────────────── */
enum {
    C_WHITE = 0, C_DEEP_BLUE, C_CYAN, C_ORANGE, C_NAVY, C_KEY_BG, C_DIVIDER, C_HINT,
    C_USER_BUBBLE, C_AI_BUBBLE, C_RAIN_DIM, C_RAIN, C_RAIN_HEAD, C_TITLE_SHADOW, C_SCRIM,
    C_COUNT
};
#define C_TEXT_TP   255   /* graphx default text background = transparent colour; never drawn */
#define C_TEXT_NOTP 254   /* an unused index: makes glyph backgrounds opaque when set as TP colour */

static const uint16_t palette[C_COUNT] = {
    gfx_RGBTo1555(0xFF, 0xFF, 0xFF),  /* WHITE */
    gfx_RGBTo1555(0x0A, 0x1A, 0x52),  /* DEEP_BLUE */
    gfx_RGBTo1555(0x3A, 0xE6, 0xFF),  /* CYAN */
    gfx_RGBTo1555(0xFF, 0x8C, 0x1A),  /* ORANGE */
    gfx_RGBTo1555(0x1A, 0x1A, 0x6B),  /* NAVY */
    gfx_RGBTo1555(0xE3, 0xE7, 0xF5),  /* KEY_BG */
    gfx_RGBTo1555(0xB9, 0xC0, 0xE0),  /* DIVIDER */
    gfx_RGBTo1555(0x7A, 0x8A, 0xC0),  /* HINT */
    gfx_RGBTo1555(0x9A, 0xE6, 0xFF),  /* USER_BUBBLE */
    gfx_RGBTo1555(0xFF, 0xB8, 0x5A),  /* AI_BUBBLE */
    gfx_RGBTo1555(0x14, 0x60, 0x7A),  /* RAIN_DIM */
    gfx_RGBTo1555(0x1F, 0xA8, 0xC8),  /* RAIN */
    gfx_RGBTo1555(0xDF, 0xFB, 0xFF),  /* RAIN_HEAD */
    gfx_RGBTo1555(0x8C, 0x3A, 0x00),  /* TITLE_SHADOW */
    gfx_RGBTo1555(0x0B, 0x1D, 0x5A),  /* SPLASH_SCRIM (solid; no alpha on CE) */
};

/* ── Screens / keys ─────────────────────────────────────────────────── */
enum { SCR_SPLASH, SCR_TYPING, SCR_THINKING, SCR_ANSWER };
enum { ST_WAITING, ST_CONNECTED, ST_DISCONNECTED };

enum { K_0, K_1, K_2, K_3, K_4, K_5, K_6, K_7, K_8, K_9,
       K_ALPHA, K_2ND, K_DEL, K_ENTER, K_CLEAR, K_UP, K_DOWN, K_MODE, K_COUNT };
#define KB(k)     (1UL << (k))
#define K_DIGITS  0x3FFUL

/* on-screen key ids: 0-9 digits, 10 ALPHA, 11 DEL */
#define KEY_ALPHA 10
#define KEY_DEL   11
static const uint8_t GRID_ORDER[12] = { 7, 8, 9, 4, 5, 6, 1, 2, 3, KEY_ALPHA, 0, KEY_DEL };

/* ── Globals ────────────────────────────────────────────────────────── */
static srl_device_t srl;
static bool has_device = false;
static uint8_t srl_buf[SRL_BUF_SZ];

static t9_t        t9;
static chat_t      chat;
static rain_col_t  rain[RAIN_COLS];
static char        rain_tab[RAIN_COLS][RAIN_LEN];   /* characters, generated once */
static bubble_t    bubbles[MAX_MSGS + 1];
static wrap_t      wrap;
static char        linebuf[INPUT_COLS + 1];

static char     resp_buf[MAX_RESP];
static uint16_t resp_len = 0;
static uint8_t  stale_replies = 0;

static uint8_t  screen = SCR_SPLASH;
static uint8_t  status = ST_WAITING;
static bool     running = true;
static bool     dirty = true;
static bool     splash_full = true;     /* splash needs a complete repaint (not just rain) */
static uint16_t scroll = 0xFFFF;        /* 0xFFFF = "bottom", clamped at draw time */
static uint16_t content_h = 0;

static int8_t   flash_key = -1;
static uint32_t flash_t0 = 0;
static uint32_t t_key = 0, t_anim = 0;
static uint16_t tick = 0;
static uint32_t prev_keys = 0;
static uint32_t last_edge[K_COUNT];

/* ── Timebase: clock() = timer 1 @ 32768 Hz (usbdrvce owns timer 2) ─── */
static uint32_t now_ms(void) {
    uint32_t t = (uint32_t)clock();
    return (t >> 15) * 1000u + (((t & 0x7FFFu) * 1000u) >> 15);
}

/* USB servicing from inside drawing loops: keeps enumeration and serial traffic
 * alive while a frame is being painted. */
static void usb_pump(void) {
#ifdef ZG_DEBUG
    usb_error_t e = usb_HandleEvents();
    if (e) dbg_last_err = (uint8_t)e;
    dbg_ev_count++;
#else
    usb_HandleEvents();
#endif
}

/* ── USB event handler (unchanged from v4) ──────────────────────────── */
static usb_error_t handle_usb_event(usb_event_t event, void *event_data,
                                     usb_callback_data_t *callback_data __attribute__((unused))) {
    usb_error_t err;
#ifdef ZG_DEBUG
    dbg_note_event((uint8_t)event);
#endif
    if ((err = srl_UsbEventCallback(event, event_data, callback_data)) != USB_SUCCESS)
        return err;
    if (event == USB_DEVICE_CONNECTED_EVENT && !(usb_GetRole() & USB_ROLE_DEVICE)) {
        usb_device_t device = event_data;
        usb_ResetDevice(device);
    }
    if (event == USB_HOST_CONFIGURE_EVENT ||
        (event == USB_DEVICE_ENABLED_EVENT && !(usb_GetRole() & USB_ROLE_DEVICE))) {
        if (has_device) return USB_SUCCESS;
        usb_device_t device;
        if (event == USB_HOST_CONFIGURE_EVENT) {
            device = usb_FindDevice(NULL, NULL, USB_SKIP_HUBS);
            if (device == NULL) return USB_SUCCESS;
        } else {
            device = event_data;
        }
        srl_error_t error = srl_Open(&srl, device, srl_buf, sizeof srl_buf,
                                      SRL_INTERFACE_ANY, 9600);
        if (error) return USB_SUCCESS;
        has_device = true;
    }
    if (event == USB_DEVICE_DISCONNECTED_EVENT) {
        usb_device_t device = event_data;
        if (device == srl.dev) {
            srl_Close(&srl);
            has_device = false;
        }
    }
    return USB_SUCCESS;
}

/* ── Serial TX (unchanged from v4) ──────────────────────────────────── */
static bool send_query(const char *text) {
    int len = (int)strlen(text);
    int sent = 0;
    while (sent < len) {
        usb_HandleEvents();
        if (!has_device) return false;
        int n = srl_Write(&srl, text + sent, len - sent);
        if (n < 0) return false;
        sent += n;
    }
    const char nl = '\n';
    int tries = 0;
    while (tries < 1000) {
        usb_HandleEvents();
        int n = srl_Write(&srl, &nl, 1);
        if (n == 1) break;
        if (n < 0) return false;
        tries++;
    }
    return true;
}

/* ── Serial RX: non-blocking, pumped in every state ─────────────────── */
static void poll_serial(void) {
    char tmp[64];
    int n, i;
    if (!has_device) return;
    n = srl_Read(&srl, tmp, sizeof tmp);
    if (n <= 0) return;
    for (i = 0; i < n; i++) {
        char c = tmp[i];
        if (c == '\n') {
            if (stale_replies) {
                stale_replies--;                     /* reply to a cancelled question */
            } else if (screen == SCR_THINKING) {
                chat_push(&chat, ROLE_AI, resp_buf);
                screen = SCR_ANSWER;
                scroll = 0xFFFF;
            }
            resp_len = 0;
            resp_buf[0] = '\0';
            dirty = true;
        } else if (c >= 0x20 && resp_len < MAX_RESP - 1) {
            resp_buf[resp_len++] = c;
            resp_buf[resp_len] = '\0';
        }
    }
}

/* ── Keypad ─────────────────────────────────────────────────────────── */
static uint32_t read_keys(void) {
    uint32_t m = 0;
    kb_Scan();
    if (kb_Data[3] & kb_0)     m |= KB(K_0);
    if (kb_Data[3] & kb_1)     m |= KB(K_1);
    if (kb_Data[4] & kb_2)     m |= KB(K_2);
    if (kb_Data[5] & kb_3)     m |= KB(K_3);
    if (kb_Data[3] & kb_4)     m |= KB(K_4);
    if (kb_Data[4] & kb_5)     m |= KB(K_5);
    if (kb_Data[5] & kb_6)     m |= KB(K_6);
    if (kb_Data[3] & kb_7)     m |= KB(K_7);
    if (kb_Data[4] & kb_8)     m |= KB(K_8);
    if (kb_Data[5] & kb_9)     m |= KB(K_9);
    if (kb_Data[2] & kb_Alpha) m |= KB(K_ALPHA);
    if (kb_Data[1] & kb_2nd)   m |= KB(K_2ND);
    if (kb_Data[1] & kb_Del)   m |= KB(K_DEL);
    if (kb_Data[1] & kb_Mode)  m |= KB(K_MODE);
    if (kb_Data[6] & kb_Enter) m |= KB(K_ENTER);
    if (kb_Data[6] & kb_Clear) m |= KB(K_CLEAR);
    if (kb_Data[7] & kb_Up)    m |= KB(K_UP);
    if (kb_Data[7] & kb_Down)  m |= KB(K_DOWN);
    return m;
}

/* Edge detection (key-down only, no auto-repeat) + per-key bounce lockout. */
static uint32_t key_edges(uint32_t now) {
    uint32_t cur = read_keys();
    uint32_t pressed = cur & ~prev_keys;
    uint8_t k;
    prev_keys = cur;
    for (k = 0; k < K_COUNT && pressed; k++) {
        if (!(pressed & KB(k))) continue;
        if ((uint32_t)(now - last_edge[k]) < KEY_LOCKOUT_MS) pressed &= ~KB(k);
        else last_edge[k] = now;
    }
    return pressed;
}

/* ── Drawing helpers ────────────────────────────────────────────────── */
static void rect(int x, int y, int w, int h, uint8_t color) {
    gfx_SetColor(color);
    gfx_FillRectangle_NoClip((uint24_t)x, (uint8_t)y, (uint24_t)w, (uint8_t)h);
}

static void text_at(const char *s, int x, int y, uint8_t color) {
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(s, x, y);
}

static void text_centered(const char *s, int y, uint8_t color, uint8_t scale) {
    int w = (int)strlen(s) * CHAR_W * scale;
    text_at(s, (SCREEN_W - w) / 2, y, color);
}

static void char_at(char c, int x, int y, uint8_t color) {
    gfx_SetTextFGColor(color);
    gfx_SetTextXY(x, y);
    gfx_PrintChar(c);
}

static void text_transparent(bool on) {
    gfx_SetTextBGColor(on ? C_TEXT_TP : C_DEEP_BLUE);
    gfx_SetTextTransparentColor(on ? C_TEXT_TP : C_TEXT_NOTP);
}

static void draw_header(const char *badge) {
    int bw = (int)strlen(badge) * CHAR_W + 8;
    int bx = 314 - bw;
    rect(0, 0, SCREEN_W, 20, C_ORANGE);
    text_at("ZACH GPT", 6, 6, C_NAVY);
    rect(bx, 4, bw, 12, C_NAVY);
    text_at(badge, bx + 4, 6, C_ORANGE);
}

/* ── Splash (drawn directly on the screen, rain repainted per column) ── */
#define RAIN_X_STEP (SCREEN_W / RAIN_COLS)

/* Repaints the visible cells of one rain column with an opaque DEEP_BLUE glyph
 * background (caller sets text_transparent(false)). Rows 10-19 are under the
 * scrim and rows >= 26 under the bottom panel, so they are never drawn. */
static void splash_draw_col(uint8_t i) {
    const rain_col_t *c = &rain[i];
    const char *s = rain_tab[i];
    uint8_t body = c->dim ? C_RAIN_DIM : C_RAIN;
    uint8_t head = c->dim ? C_RAIN : C_RAIN_HEAD;
    int x = i * RAIN_X_STEP;
    uint8_t j, r = c->pos;                          /* row of string index 0 (the head) */
    for (j = 0; j < RAIN_LEN; j++) {
        if (r <= 25 && !(r >= 10 && r <= 19))
            char_at(s[j], x, r * FONT_H, j < 2 ? head : body);
        if (++r == RAIN_LEN) r = 0;
    }
}

/* Restores the scrim / panel edges that opaque rain cells overlap. */
static void splash_fix_edges(void) {
    rect(0, 76, SCREEN_W, 4, C_SCRIM);              /* row 9 reaches y 79 */
    rect(0, 160, SCREEN_W, 4, C_SCRIM);             /* row 20 starts under the scrim */
    rect(0, 206, SCREEN_W, 2, C_DEEP_BLUE);         /* row 25 reaches y 207 */
}

static void splash_draw_status(void) {
    char line[40];
    const char *base;
    uint8_t dots, j, k;
    if (status == ST_CONNECTED) { base = "USB CONNECTED"; dots = 0; }
    else if (status == ST_DISCONNECTED) { base = "USB DISCONNECTED"; dots = (uint8_t)((tick / 4) % 4); }
    else { base = "WAITING FOR USB CONNECTION"; dots = (uint8_t)((tick / 4) % 4); }
    k = (uint8_t)strlen(base);
    memcpy(line, base, k);
    for (j = 0; j < dots; j++) line[k++] = '.';
    line[k] = '\0';
    rect(0, 214, SCREEN_W, FONT_H, C_DEEP_BLUE);
    text_centered(line, 214, C_WHITE, 1);
}

static void splash_draw_hint(void) {
    rect(0, 228, SCREEN_W, FONT_H, C_DEEP_BLUE);
#ifdef ZG_DEBUG
    {
        char line[40];
        uint8_t j, k;
        snprintf(line, sizeof line, "T%lu E%lu X%u F%u R%u V", (unsigned long)(dbg_now / 1000),
                 (unsigned long)dbg_ev_rate, (unsigned)dbg_last_err, (unsigned)dbg_frame_ms,
                 (unsigned)usb_GetRole());
        k = (uint8_t)strlen(line);
        for (j = 0; j < dbg_nevents && k < sizeof line - 2; j++)
            line[k++] = (char)(dbg_events[j] < 10 ? '0' + dbg_events[j] : 'A' + dbg_events[j] - 10);
        line[k] = '\0';
        text_centered(line, 228, C_CYAN, 1);
    }
#else
    text_centered(status == ST_CONNECTED ? "press ENTER to start"
                  : status == ST_DISCONNECTED ? "reconnect cable and wait"
                  : "run bridge.py on your PC", 228, C_HINT, 1);
#endif
}

/* Complete splash repaint. */
static void draw_splash(void) {
    uint8_t i;
    gfx_FillScreen(C_DEEP_BLUE);
    text_transparent(false);
    for (i = 0; i < RAIN_COLS; i++) { splash_draw_col(i); usb_pump(); }
    text_transparent(true);

    /* Scrim + title with drop shadow (scale 4 = 32 px; closest to the 36 px design). */
    rect(0, 76, SCREEN_W, 88, C_SCRIM);
    gfx_SetTextScale(4, 4);
    text_at("ZACH GPT", 36, 100, C_TITLE_SHADOW);
    usb_pump();
    text_at("ZACH GPT", 32, 96, C_ORANGE);
    gfx_SetTextScale(1, 1);
    usb_pump();

    /* Subtitle; the middle dot is not in the 8x8 font, so draw it as a 2x2 block. */
    text_centered("AI ON YOUR TI-84     v5.0", 140, C_CYAN, 1);
    rect(207, 143, 2, 2, C_CYAN);

    rect(0, 206, SCREEN_W, 34, C_DEEP_BLUE);
    splash_draw_status();
    splash_draw_hint();
}

/* Per-tick splash update: only the columns that advanced, plus the dots. */
static void draw_splash_tick(void) {
    uint8_t i;
    bool any = false;
    text_transparent(false);
    for (i = 0; i < RAIN_COLS; i++) {
        if (rain[i].moved) { splash_draw_col(i); any = true; }
        usb_pump();
    }
    text_transparent(true);
    if (any) splash_fix_edges();
    if (tick % 4 == 0 && status != ST_CONNECTED) splash_draw_status();
#ifdef ZG_DEBUG
    splash_draw_hint();
#endif
}

/* ── Typing ─────────────────────────────────────────────────────────── */
static void draw_key(uint8_t key, int x, int y) {
    bool hl = (key <= 9 && t9.pend_key == (int8_t)key) || flash_key == (int8_t)key;
    uint8_t fg = hl ? C_WHITE : C_NAVY;
    char d[2];

    rect(x, y, 100, 32, hl ? C_ORANGE : C_KEY_BG);

    if (key == KEY_ALPHA) {
        text_at(t9.num_mode ? "ALPHA ABC" : "ALPHA 123", x + 6, y + 12, fg);
        return;
    }
    if (key == KEY_DEL) {
        text_at("DEL BACK", x + 6, y + 12, fg);
        return;
    }

    d[0] = (char)('0' + key);
    d[1] = '\0';
    gfx_SetTextScale(2, 2);
    text_at(d, x + 6, y + 8, fg);
    gfx_SetTextScale(1, 1);

    if (key == 0 && !t9.num_mode) {
        text_at("SPACE", x + 34, y + 12, fg);
    } else {
        const char *ls = t9_letters(&t9, key);
        uint8_t j;
        for (j = 0; ls[j]; j++) {
            int cx = x + 34 + j * 10;
            bool on = hl && t9.pend_key == (int8_t)key && j == t9.pend_idx;
            if (on) rect(cx, y + 12, 9, 10, C_NAVY);
            char_at(ls[j], cx, y + 13, on ? C_ORANGE : fg);
        }
    }
}

static void draw_typing(void) {
    uint8_t i;
    int cx;

    gfx_FillScreen(C_WHITE);
    draw_header(t9.num_mode ? "123" : "ABC");

    /* Cycle strip (y 26). */
    if (t9.pend_key >= 0) {
        const char *ls = t9_letters(&t9, (uint8_t)t9.pend_key);
        uint8_t n = (uint8_t)strlen(ls);
        int x0 = (SCREEN_W - (n * 16 - 4)) / 2;
        for (i = 0; i < n; i++) {
            bool on = (i == t9.pend_idx);
            int x = x0 + i * 16;
            rect(x, 26, 12, 12, on ? C_ORANGE : C_KEY_BG);
            char_at(ls[i], x + 2, 28, on ? C_WHITE : C_NAVY);
        }
    } else {
        text_centered(t9.num_mode ? "TAP TWICE FOR SYMBOL" : "TAP KEY TO CYCLE LETTERS", 28, C_HINT, 1);
    }
    usb_pump();

    /* Prompt + input (2 lines, bottom-anchored, 37 cols, word-wrapped). */
    text_at(">", 6, 46, C_ORANGE);
    wrap_text(t9.text, t9.len, INPUT_COLS, t9.pend_key >= 0 ? 2 : 1, &wrap);
    for (i = 0; i < wrap.count; i++) {
        int back = wrap.cursor_line - i;          /* lines above the cursor line */
        if (back > 1) continue;                    /* only the last two lines are visible */
        memcpy(linebuf, t9.text + wrap.start[i], wrap.len[i]);
        linebuf[wrap.len[i]] = '\0';
        text_at(linebuf, 18, 62 - back * 10, C_NAVY);
        usb_pump();
    }
    cx = 18 + wrap.cursor_col * CHAR_W;
    if (t9.pend_key >= 0) {
        char p[2];
        p[0] = t9_pending_char(&t9);
        p[1] = '\0';
        rect(cx, 58, 14, 16, C_ORANGE);
        gfx_SetTextScale(2, 2);
        text_at(p, cx + 1, 58, C_WHITE);
        gfx_SetTextScale(1, 1);
    } else if ((tick % 10) < 5) {
        rect(cx, 61, 8, 10, C_NAVY);               /* caret, 1 s blink */
    }

    gfx_SetColor(C_DIVIDER);
    gfx_HorizLine_NoClip(0, 74, SCREEN_W);

    /* Key grid mirrors the physical keypad: 7 8 9 / 4 5 6 / 1 2 3 / ALPHA 0 DEL. */
    for (i = 0; i < 12; i++) {
        draw_key(GRID_ORDER[i], 6 + (i % 3) * 104, 80 + (i / 3) * 36);
        usb_pump();
    }

    text_centered(t9.num_mode ? "ENTER=send DEL=back ALPHA=ABC 2nd=exit"
                              : "ENTER=send DEL=back ALPHA=123 2nd=exit", 230, C_HINT, 1);
}

/* ── Thinking / Answer ──────────────────────────────────────────────── */
static void draw_spinner(int x, int y, uint8_t bg) {
    uint8_t step = (uint8_t)((tick / 3) % 4);
    gfx_SetColor(C_NAVY);
    gfx_FillRectangle(x, y, 8, 8);
    gfx_SetColor(bg);
    gfx_FillRectangle(x + ((step == 1 || step == 2) ? 5 : 0),
                      y + ((step == 2 || step == 3) ? 5 : 0), 3, 3);
}

static void draw_chat(bool thinking) {
    uint8_t n, i, ty, th;
    uint16_t maxs;

    gfx_FillScreen(C_WHITE);
    draw_header("USB OK");

    content_h = chat_layout(&chat, thinking, bubbles, &n);
    maxs = scroll_max(content_h, CHAT_VIEW_H);
    if (scroll > maxs) scroll = maxs;

    /* Viewport y 28-224, x 0-312: clipped drawing only here (clipped text ignores scale). */
    gfx_SetClipRegion(0, 28, 312, 224);
    gfx_SetTextConfig(gfx_text_clip);
    for (i = 0; i < n; i++) {
        const bubble_t *b = &bubbles[i];
        int by = 28 + (int)b->y - (int)scroll;
        uint8_t bg = (b->role == ROLE_USER) ? C_USER_BUBBLE : C_AI_BUBBLE;
        if (by + (int)b->h <= 28 || by >= 224) continue;
        gfx_SetColor(bg);
        gfx_FillRectangle(b->x, by, b->w, b->h);
        if (b->cols == 0) {                        /* THINKING pill */
            draw_spinner(b->x + 6, by + 7, bg);
            text_at("THINKING...", b->x + 22, by + 7, C_NAVY);
        } else {
            const msg_t *m = chat_get(&chat, i);
            uint8_t l;
            wrap_text(m->text, (uint16_t)strlen(m->text), b->cols, 0, &wrap);
            for (l = 0; l < wrap.count; l++) {
                int ly = by + 7 + l * 10;
                if (ly + FONT_H <= 28 || ly >= 224) continue;   /* line fully outside */
                memcpy(linebuf, m->text + wrap.start[l], wrap.len[l]);
                linebuf[wrap.len[l]] = '\0';
                text_at(linebuf, b->x + 6, ly, C_NAVY);
                usb_pump();
            }
        }
        usb_pump();
    }
    gfx_SetTextConfig(gfx_text_noclip);
    gfx_SetClipRegion(0, 0, SCREEN_W, SCREEN_H);

    /* Scrollbar. */
    rect(312, 28, 4, CHAT_VIEW_H, C_KEY_BG);
    scroll_thumb(content_h, CHAT_VIEW_H, scroll, 28, &ty, &th);
    rect(312, ty, 4, th, C_ORANGE);

    text_centered(thinking ? "CLEAR=cancel" : "UP/DOWN=scroll ENTER=new CLEAR=exit", 230, C_HINT, 1);
}

/* Full frame of the current screen (the splash variant is used by the host renderer). */
static void draw_current(void) {
    switch (screen) {
        case SCR_SPLASH:   draw_splash(); break;
        case SCR_TYPING:   draw_typing(); break;
        case SCR_THINKING: draw_chat(true); break;
        default:           draw_chat(false); break;
    }
}

/* ── State transitions ──────────────────────────────────────────────── */
static void enter_splash(uint8_t new_status) {
    screen = SCR_SPLASH;
    status = new_status;
    chat_init(&chat);
    t9_set_text(&t9, "");
    stale_replies = 0;
    resp_len = 0;
    resp_buf[0] = '\0';
    flash_key = -1;
    gfx_SetDrawScreen();                 /* splash paints the visible buffer in place */
    splash_full = true;
    dirty = true;
}

static void enter_typing(void) {
    screen = SCR_TYPING;
    t9_set_text(&t9, "");
    flash_key = -1;
    gfx_SetDrawBuffer();                 /* buffered screens: full frame + SwapDraw */
    dirty = true;
}

static void flash(uint8_t key, uint32_t now) {
    flash_key = (int8_t)key;
    flash_t0 = now;
}

static void handle_keys(uint32_t p, uint32_t now) {
    uint8_t d;
    switch (screen) {
    case SCR_SPLASH:
        if (p & KB(K_CLEAR)) { running = false; return; }
        if (status == ST_CONNECTED && (p & (K_DIGITS | KB(K_ENTER) | KB(K_ALPHA))))
            enter_typing();
        return;

    case SCR_TYPING:
        for (d = 0; d <= 9; d++)
            if (p & KB(d)) { t9_digit(&t9, d, now); flash(d, now); }
        if (p & KB(K_ALPHA)) { t9_alpha(&t9); flash(KEY_ALPHA, now); }
        if (p & KB(K_DEL))   { t9_del(&t9);   flash(KEY_DEL, now); }
        if (p & KB(K_ENTER)) {
            if (t9_enter(&t9)) {
                chat_push(&chat, ROLE_USER, t9.text);
                if (send_query(t9.text)) {
                    t9_set_text(&t9, "");
                    resp_len = 0;
                    resp_buf[0] = '\0';
                    scroll = 0xFFFF;
                    screen = SCR_THINKING;
                } else if (has_device) {
                    enter_splash(ST_DISCONNECTED);  /* write failed with a device present */
                }
                /* !has_device: the main loop drops to the splash */
            }
        }
        if (p & KB(K_2ND)) enter_splash(ST_CONNECTED);
        return;

    case SCR_THINKING:
        if (p & KB(K_CLEAR)) {
            chat_pop_last(&chat, resp_buf, MAX_RESP);  /* restore the question */
            t9_set_text(&t9, resp_buf);
            resp_len = 0;
            resp_buf[0] = '\0';
            stale_replies++;                           /* skip the reply when it arrives */
            screen = SCR_TYPING;
            flash_key = -1;
        }
        return;

    default: { /* SCR_ANSWER */
        uint16_t maxs = scroll_max(content_h, CHAT_VIEW_H);
        if (scroll > maxs) scroll = maxs;
        if (p & KB(K_UP))   scroll = (scroll >= 20) ? (uint16_t)(scroll - 20) : 0;
        if (p & KB(K_DOWN)) scroll = (scroll + 20 > maxs) ? maxs : (uint16_t)(scroll + 20);
        if (p & KB(K_ENTER)) enter_typing();
        if (p & (KB(K_CLEAR) | KB(K_2ND))) enter_splash(ST_CONNECTED);
        return;
    }
    }
}

/* ── Main ───────────────────────────────────────────────────────────── */
int main(void) {
    uint8_t i;

    gfx_Begin();
    gfx_SetPalette(palette, sizeof palette, 0);
    gfx_SetMonospaceFont(CHAR_W);
    text_transparent(true);                     /* text BG == transparent: glyphs never paint their cell */
    gfx_SetTextConfig(gfx_text_noclip);
    gfx_SetDrawScreen();                        /* the splash draws in place */

    rain_init(rain, 7);
    for (i = 0; i < RAIN_COLS; i++) rain_chars(&rain[i], rain_tab[i]);
    t9_init(&t9);
    chat_init(&chat);
    memset(last_edge, 0, sizeof last_edge);

    const usb_standard_descriptors_t *desc = srl_GetCDCStandardDescriptors();
    if (usb_Init(handle_usb_event, NULL, desc, USB_DEFAULT_INIT_FLAGS) != USB_SUCCESS) {
        gfx_FillScreen(C_DEEP_BLUE);
        text_centered("USB INIT FAILED", 110, C_ORANGE, 1);
        text_centered("press any key to exit", 124, C_HINT, 1);
        do { kb_Scan(); } while (kb_AnyKey());
        do { kb_Scan(); } while (!kb_AnyKey());
        usb_Cleanup();
        gfx_End();
        return 1;
    }

    t_key = t_anim = now_ms();
    prev_keys = read_keys();            /* keys held at launch (e.g. ENTER) are not edges */

    while (running) {
        uint32_t now = now_ms();

        usb_pump();
#ifdef ZG_DEBUG
        dbg_now = now;
        if ((uint32_t)(now - dbg_ev_t0) >= 1000) { dbg_ev_rate = dbg_ev_count; dbg_ev_count = 0; dbg_ev_t0 = now; dirty = true; }
#endif

        /* Connection state → splash status. */
        if (!has_device) {
            if (screen != SCR_SPLASH) enter_splash(ST_DISCONNECTED);
            else if (status == ST_CONNECTED) { status = ST_DISCONNECTED; splash_full = true; dirty = true; }
        } else if (screen == SCR_SPLASH && status != ST_CONNECTED) {
            status = ST_CONNECTED;
            splash_full = true;
            dirty = true;
        }

        /* Keys: sampled at 100 Hz, key-down edges only. */
        if ((uint32_t)(now - t_key) >= KEY_SAMPLE_MS) {
            uint32_t pressed;
            t_key = now;
            pressed = key_edges(now);
            if (pressed) { handle_keys(pressed, now); dirty = true; }
        }

        poll_serial();

        /* Timers. */
        if (screen == SCR_TYPING && t9.pend_key >= 0) {
            t9_tick(&t9, now);
            if (t9.pend_key < 0) dirty = true;              /* 900 ms commit */
        }
        if (flash_key >= 0 && (uint32_t)(now - flash_t0) >= FLASH_MS) {
            flash_key = -1;
            dirty = true;
        }
        if ((uint32_t)(now - t_anim) >= ANIM_TICK_MS) {
            t_anim = now;
            tick++;
            rain_step(rain, (uint8_t)tick);
            if (screen == SCR_SPLASH) dirty = true;                               /* rain + dots */
            else if (screen == SCR_THINKING && tick % 3 == 0) dirty = true;       /* spinner */
            else if (screen == SCR_TYPING && t9.pend_key < 0 && tick % 5 == 0) dirty = true;  /* caret */
        }

        if (dirty) {
#ifdef ZG_DEBUG
            uint32_t t0 = now_ms();
#endif
            dirty = false;
            if (screen == SCR_SPLASH) {
                if (splash_full) { splash_full = false; draw_splash(); }
                else draw_splash_tick();
            } else {
                draw_current();          /* always a full frame: SwapDraw does not copy */
                gfx_SwapDraw();
            }
#ifdef ZG_DEBUG
            dbg_frame_ms = (uint16_t)(now_ms() - t0);
#endif
        }
    }

    usb_Cleanup();
    gfx_End();
    return 0;
}
