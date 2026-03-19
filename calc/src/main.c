/*
 * AICHAT v2.0 — TI-84 Plus CE AI Chat Bridge
 * On-screen QWERTY keyboard with arrow key navigation.
 * Libraries: srldrvce, graphx, keypadc
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <graphx.h>
#include <keypadc.h>
#include <srldrvce.h>
#include <usbdrvce.h>
#include <tice.h>

/* ── Config ── */
#define MAX_INPUT   256
#define MAX_RESP    512
#define SRL_BUF_SZ  4096

#define SCREEN_W    320
#define SCREEN_H    240
#define FONT_H      8
#define CHAR_W      8
#define CHARS_PER_LINE (SCREEN_W / CHAR_W)

/* Colors */
#define COLOR_BG      0x00   /* black */
#define COLOR_FG      0xFF   /* white */
#define COLOR_PROMPT  0xE7   /* yellow */
#define COLOR_WAIT    0x03   /* green */
#define COLOR_KEY_BG  0x18   /* dark gray for key cells */
#define COLOR_KEY_HL  0xE7   /* yellow highlight */
#define COLOR_KEY_FG  0xFF   /* white text on keys */
#define COLOR_KEY_HFG 0x00   /* black text on highlight */
#define COLOR_DIVIDER 0x18   /* gray divider line */

/* ── Keyboard layout ── */
#define KB_TOP_Y      136    /* keyboard starts here */
#define KB_KEY_W      28     /* key width in pixels */
#define KB_KEY_H      22     /* key height in pixels */
#define KB_PAD        2      /* padding between keys */
#define KB_ROWS       4      /* 3 letter rows + 1 control row */

/* Row lengths */
#define ROW0_LEN  10   /* QWERTYUIOP */
#define ROW1_LEN  9    /* ASDFGHJKL */
#define ROW2_LEN  7    /* ZXCVBNM */
#define ROW3_LEN  3    /* SPC DEL SEND */

static const char row0_chars[] = "QWERTYUIOP";
static const char row1_chars[] = "ASDFGHJKL";
static const char row2_chars[] = "ZXCVBNM";
static const char *row3_labels[] = {"SPACE", "DEL", "SEND"};

static const int row_lens[] = {ROW0_LEN, ROW1_LEN, ROW2_LEN, ROW3_LEN};

/* Starting X offset for each row (to center shorter rows) */
static int row_x_offset(int row) {
    switch (row) {
        case 0: return (SCREEN_W - ROW0_LEN * (KB_KEY_W + KB_PAD)) / 2;
        case 1: return (SCREEN_W - ROW1_LEN * (KB_KEY_W + KB_PAD)) / 2;
        case 2: return (SCREEN_W - ROW2_LEN * (KB_KEY_W + KB_PAD)) / 2;
        case 3: return 4;  /* controls row left-aligned with padding */
        default: return 0;
    }
}

/* Cursor position on the keyboard */
static int kb_row = 0;
static int kb_col = 0;

/* ── Globals ── */
static srl_device_t srl;
static bool has_device = false;
static uint8_t srl_buf[SRL_BUF_SZ];

static char input_buf[MAX_INPUT];
static int input_len = 0;
static char resp_buf[MAX_RESP];

/* ── Draw helpers ── */
static int text_y = 0;

static void clear_top_area(void) {
    gfx_SetColor(COLOR_BG);
    gfx_FillRectangle_NoClip(0, 0, SCREEN_W, KB_TOP_Y - 2);
    text_y = 0;
}

static void print_top_line(const char *text, uint8_t color) {
    if (text_y >= KB_TOP_Y - FONT_H) return;
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(text, 0, text_y);
    text_y += FONT_H;
}

static void print_top_wrapped(const char *text, uint8_t color, int max_y) {
    char line[CHARS_PER_LINE + 1];
    int len = (int)strlen(text);
    int pos = 0;
    gfx_SetTextFGColor(color);
    while (pos < len && text_y < max_y) {
        int chunk = len - pos;
        if (chunk > CHARS_PER_LINE) chunk = CHARS_PER_LINE;
        memcpy(line, text + pos, chunk);
        line[chunk] = '\0';
        gfx_PrintStringXY(line, 0, text_y);
        text_y += FONT_H;
        pos += chunk;
    }
}

/* Draw the input line at the top of screen */
static void draw_input_line(void) {
    char display[CHARS_PER_LINE + 1];
    int show_len;

    /* Clear input area (first 2 lines) */
    gfx_SetColor(COLOR_BG);
    gfx_FillRectangle_NoClip(0, 0, SCREEN_W, FONT_H * 2);

    /* "> " prefix */
    gfx_SetTextFGColor(COLOR_PROMPT);
    gfx_PrintStringXY("> ", 0, 0);

    /* Show last N chars of input + cursor */
    show_len = input_len;
    if (show_len > CHARS_PER_LINE - 3) show_len = CHARS_PER_LINE - 3;
    memcpy(display, input_buf + (input_len - show_len), show_len);
    display[show_len] = '_';
    display[show_len + 1] = '\0';

    gfx_SetTextFGColor(COLOR_FG);
    gfx_PrintStringXY(display, CHAR_W * 2, 0);
}

/* ── On-screen keyboard drawing ── */
static void draw_keyboard(void) {
    char ch_str[2] = {0, 0};

    /* Draw divider line */
    gfx_SetColor(COLOR_DIVIDER);
    gfx_HorizLine_NoClip(0, KB_TOP_Y - 2, SCREEN_W);

    /* Draw letter rows 0-2 */
    for (int r = 0; r < 3; r++) {
        int x_off = row_x_offset(r);
        int y = KB_TOP_Y + r * (KB_KEY_H + KB_PAD);
        int len = row_lens[r];
        const char *chars;

        switch (r) {
            case 0: chars = row0_chars; break;
            case 1: chars = row1_chars; break;
            default: chars = row2_chars; break;
        }

        for (int c = 0; c < len; c++) {
            int x = x_off + c * (KB_KEY_W + KB_PAD);
            bool highlighted = (r == kb_row && c == kb_col);

            /* Key background */
            gfx_SetColor(highlighted ? COLOR_KEY_HL : COLOR_KEY_BG);
            gfx_FillRectangle_NoClip(x, y, KB_KEY_W, KB_KEY_H);

            /* Letter centered in key */
            ch_str[0] = chars[c];
            int tx = x + (KB_KEY_W - CHAR_W) / 2;
            int ty = y + (KB_KEY_H - FONT_H) / 2;
            gfx_SetTextFGColor(highlighted ? COLOR_KEY_HFG : COLOR_KEY_FG);
            gfx_PrintStringXY(ch_str, tx, ty);
        }
    }

    /* Draw control row (row 3) */
    {
        int y = KB_TOP_Y + 3 * (KB_KEY_H + KB_PAD);
        /* SPACE key: wider */
        int space_w = 160;
        int del_w = 60;
        int send_w = 70;
        int gap = 8;
        int x = 4;

        /* SPACE */
        bool hl = (kb_row == 3 && kb_col == 0);
        gfx_SetColor(hl ? COLOR_KEY_HL : COLOR_KEY_BG);
        gfx_FillRectangle_NoClip(x, y, space_w, KB_KEY_H);
        gfx_SetTextFGColor(hl ? COLOR_KEY_HFG : COLOR_KEY_FG);
        gfx_PrintStringXY("SPACE", x + (space_w - 5 * CHAR_W) / 2, y + (KB_KEY_H - FONT_H) / 2);
        x += space_w + gap;

        /* DEL */
        hl = (kb_row == 3 && kb_col == 1);
        gfx_SetColor(hl ? COLOR_KEY_HL : COLOR_KEY_BG);
        gfx_FillRectangle_NoClip(x, y, del_w, KB_KEY_H);
        gfx_SetTextFGColor(hl ? COLOR_KEY_HFG : COLOR_KEY_FG);
        gfx_PrintStringXY("DEL", x + (del_w - 3 * CHAR_W) / 2, y + (KB_KEY_H - FONT_H) / 2);
        x += del_w + gap;

        /* SEND */
        hl = (kb_row == 3 && kb_col == 2);
        gfx_SetColor(hl ? COLOR_KEY_HL : COLOR_KEY_BG);
        gfx_FillRectangle_NoClip(x, y, send_w, KB_KEY_H);
        gfx_SetTextFGColor(hl ? COLOR_KEY_HFG : COLOR_KEY_FG);
        gfx_PrintStringXY("SEND", x + (send_w - 4 * CHAR_W) / 2, y + (KB_KEY_H - FONT_H) / 2);
    }
}

/* ── USB event handler ── */
static usb_error_t handle_usb_event(usb_event_t event, void *event_data,
                                     usb_callback_data_t *callback_data __attribute__((unused))) {
    usb_error_t err;

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

/* ── Serial I/O ── */
static bool read_line(char *buf, int max_len) {
    int pos = 0;
    memset(buf, 0, max_len);
    while (pos < max_len - 1) {
        kb_Scan();
        if (kb_IsDown(kb_KeyClear)) return false;
        usb_HandleEvents();
        if (!has_device) continue;
        char tmp[64];
        int n = srl_Read(&srl, tmp, sizeof tmp);
        if (n <= 0) continue;
        for (int i = 0; i < n && pos < max_len - 1; i++) {
            if (tmp[i] == '\n') {
                buf[pos] = '\0';
                return true;
            }
            if (tmp[i] >= 0x20) {
                buf[pos++] = tmp[i];
            }
        }
    }
    buf[pos] = '\0';
    return true;
}

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

/* ── Keyboard input handling ── */

/* Clamp column to valid range for current row */
static void clamp_col(void) {
    if (kb_col >= row_lens[kb_row])
        kb_col = row_lens[kb_row] - 1;
    if (kb_col < 0) kb_col = 0;
}

/* Get the character for the current keyboard cursor position.
 * Returns 0 for special keys (DEL, SEND). */
static char get_selected_char(void) {
    switch (kb_row) {
        case 0: return row0_chars[kb_col];
        case 1: return row1_chars[kb_col];
        case 2: return row2_chars[kb_col];
        case 3:
            if (kb_col == 0) return ' ';  /* SPACE */
            return 0;  /* DEL or SEND */
        default: return 0;
    }
}

/* Returns: 0 = still typing, 1 = send, -1 = exit */
static int handle_keyboard_input(void) {
    static bool prev_up = false, prev_down = false;
    static bool prev_left = false, prev_right = false;
    static bool prev_enter = false, prev_clear = false;
    static bool prev_del = false, prev_zero = false;

    kb_Scan();

    bool up    = kb_IsDown(kb_KeyUp);
    bool down  = kb_IsDown(kb_KeyDown);
    bool left  = kb_IsDown(kb_KeyLeft);
    bool right = kb_IsDown(kb_KeyRight);
    bool enter = kb_IsDown(kb_KeyEnter);
    bool clear = kb_IsDown(kb_KeyClear);
    bool del   = kb_IsDown(kb_KeyDel);
    bool zero  = (kb_Data[3] & kb_0) != 0;

    /* Edge detection — act on press, not hold */

    /* Arrow navigation */
    if (up && !prev_up) {
        kb_row--;
        if (kb_row < 0) kb_row = KB_ROWS - 1;
        clamp_col();
    }
    if (down && !prev_down) {
        kb_row++;
        if (kb_row >= KB_ROWS) kb_row = 0;
        clamp_col();
    }
    if (left && !prev_left) {
        kb_col--;
        if (kb_col < 0) {
            /* Wrap to previous row's end */
            kb_row--;
            if (kb_row < 0) kb_row = KB_ROWS - 1;
            kb_col = row_lens[kb_row] - 1;
        }
    }
    if (right && !prev_right) {
        kb_col++;
        if (kb_col >= row_lens[kb_row]) {
            /* Wrap to next row's start */
            kb_row++;
            if (kb_row >= KB_ROWS) kb_row = 0;
            kb_col = 0;
        }
    }

    /* CLEAR = exit */
    if (clear && !prev_clear) {
        prev_up = up; prev_down = down; prev_left = left; prev_right = right;
        prev_enter = enter; prev_clear = clear; prev_del = del; prev_zero = zero;
        return -1;
    }

    /* Physical DEL key = backspace shortcut */
    if (del && !prev_del) {
        if (input_len > 0) {
            input_len--;
            input_buf[input_len] = '\0';
        }
    }

    /* Physical 0 key = space shortcut */
    if (zero && !prev_zero) {
        if (input_len < MAX_INPUT - 1) {
            input_buf[input_len++] = ' ';
            input_buf[input_len] = '\0';
        }
    }

    /* ENTER = select key on on-screen keyboard */
    if (enter && !prev_enter) {
        if (kb_row == 3) {
            if (kb_col == 0) {
                /* SPACE */
                if (input_len < MAX_INPUT - 1) {
                    input_buf[input_len++] = ' ';
                    input_buf[input_len] = '\0';
                }
            } else if (kb_col == 1) {
                /* DEL */
                if (input_len > 0) {
                    input_len--;
                    input_buf[input_len] = '\0';
                }
            } else if (kb_col == 2) {
                /* SEND */
                prev_up = up; prev_down = down; prev_left = left; prev_right = right;
                prev_enter = enter; prev_clear = clear; prev_del = del; prev_zero = zero;
                return 1;
            }
        } else {
            /* Letter key */
            char c = get_selected_char();
            if (c != 0 && input_len < MAX_INPUT - 1) {
                input_buf[input_len++] = c;
                input_buf[input_len] = '\0';
            }
        }
    }

    prev_up = up; prev_down = down; prev_left = left; prev_right = right;
    prev_enter = enter; prev_clear = clear; prev_del = del; prev_zero = zero;
    return 0;
}

/* ── Main ── */
int main(void) {
    gfx_Begin();
    gfx_SetMonospaceFont(CHAR_W);
    gfx_SetTextBGColor(COLOR_BG);
    gfx_SetTextTransparentColor(COLOR_BG);
    gfx_FillScreen(COLOR_BG);

    text_y = 0;
    print_top_line("AICHAT v2.0", COLOR_PROMPT);
    print_top_line("Connect USB and start bridge.py", COLOR_FG);
    print_top_line("Arrows=move ENTER=select CLEAR=exit", COLOR_FG);

    /* Init USB */
    const usb_standard_descriptors_t *desc = srl_GetCDCStandardDescriptors();
    usb_error_t usb_err = usb_Init(handle_usb_event, NULL, desc, USB_DEFAULT_INIT_FLAGS);
    if (usb_err) {
        print_top_line("USB init failed!", 0xE0);
        print_top_line("Press any key to exit.", COLOR_FG);
        while (!kb_AnyKey()) { kb_Scan(); }
        gfx_End();
        return 1;
    }

    print_top_line("Waiting for USB connection...", COLOR_WAIT);
    while (!has_device) {
        kb_Scan();
        if (kb_IsDown(kb_KeyClear)) {
            usb_Cleanup();
            gfx_End();
            return 0;
        }
        usb_HandleEvents();
    }

    /* Main chat loop */
    bool running = true;
    while (running) {
        /* Reset input */
        input_len = 0;
        input_buf[0] = '\0';
        kb_row = 0;
        kb_col = 0;

        /* Draw typing UI */
        gfx_FillScreen(COLOR_BG);
        draw_input_line();
        draw_keyboard();

        /* Collect input via on-screen keyboard */
        int result;
        do {
            result = handle_keyboard_input();
            if (result == -1) {
                running = false;
                break;
            }
            usb_HandleEvents();
            draw_input_line();
            draw_keyboard();
            delay(80);
        } while (result == 0);

        if (!running || input_len == 0) continue;

        /* Show waiting screen */
        gfx_FillScreen(COLOR_BG);
        text_y = 0;
        print_top_line("You:", COLOR_PROMPT);
        print_top_wrapped(input_buf, COLOR_FG, KB_TOP_Y - 10);
        text_y += FONT_H;
        print_top_line("Waiting for AI response...", COLOR_WAIT);

        /* Send query */
        if (!send_query(input_buf)) {
            print_top_line("Send failed! USB disconnected?", 0xE0);
            print_top_line("Press any key...", COLOR_FG);
            while (!kb_AnyKey()) { kb_Scan(); }
            while (kb_AnyKey()) { kb_Scan(); }
            continue;
        }

        /* Read response */
        if (!read_line(resp_buf, MAX_RESP)) {
            running = false;
            continue;
        }

        /* Display response (full screen) */
        gfx_FillScreen(COLOR_BG);
        text_y = 0;
        print_top_line("You:", COLOR_PROMPT);
        print_top_wrapped(input_buf, COLOR_FG, SCREEN_H);
        text_y += FONT_H;
        print_top_line("AI:", COLOR_PROMPT);
        print_top_wrapped(resp_buf, COLOR_FG, SCREEN_H - FONT_H);
        gfx_SetTextFGColor(COLOR_WAIT);
        gfx_PrintStringXY("[Any key -> new query]", 0, SCREEN_H - FONT_H);

        /* Wait for keypress */
        while (!kb_AnyKey()) {
            kb_Scan();
            usb_HandleEvents();
        }
        while (kb_AnyKey()) { kb_Scan(); }
    }

    usb_Cleanup();
    gfx_End();
    return 0;
}
