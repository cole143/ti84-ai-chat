/*
 * AICHAT — TI-84 Plus CE AI Chat Bridge
 *
 * Collects keyboard input, sends queries over USB CDC serial
 * to a PC bridge script, and displays AI responses on screen.
 *
 * Libraries: srldrvce, graphx, keypadc
 * Build: CE Toolchain v11+
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

#define FONT_H      8
#define CHAR_W      8
#define SCREEN_W    320
#define SCREEN_H    240
#define CHARS_PER_LINE (SCREEN_W / CHAR_W)  /* 40 */
#define MAX_LINES   (SCREEN_H / FONT_H)    /* 30 */

#define COLOR_BG    0x00   /* black */
#define COLOR_FG    0xFF   /* white */
#define COLOR_PROMPT 0xE7  /* yellow */
#define COLOR_WAIT  0x03   /* green */

/* ── Globals ── */
static srl_device_t srl;
static bool has_device = false;
static uint8_t srl_buf[SRL_BUF_SZ];

static char input_buf[MAX_INPUT];
static int input_len = 0;

static char resp_buf[MAX_RESP];

/* ── Key-to-char mapping ── */
/* Alpha mode: maps calculator keys to letters.
 * The TI-84 keyboard layout maps keys to letters A-Z when ALPHA is active.
 * We handle this manually for the graphx environment. */

/* Letters mapped to number/function keys in ALPHA mode */
static const char alpha_map_g3[] = {
    /* kb_0=bit0 */ ' ',
    /* kb_1=bit1 */ 'Y',
    /* kb_4=bit2 */ 'U',
    /* kb_7=bit3 */ 'R',
    /* kb_Comma=bit4 */ 'J',
    /* kb_Sin=bit5 */ 'E',
    /* kb_Apps=bit6 */ 'B',
    /* kb_GraphVar=bit7 */ 0
};

static const char alpha_map_g4[] = {
    /* kb_DecPnt=bit0 */ ':',
    /* kb_2=bit1 */ 'Z',
    /* kb_5=bit2 */ 'V',
    /* kb_8=bit3 */ 'S',
    /* kb_LParen=bit4 */ 'K',
    /* kb_Cos=bit5 */ 'F',
    /* kb_Prgm=bit6 */ 'C',
    /* kb_Stat=bit7 */ 0
};

static const char alpha_map_g5[] = {
    /* kb_Chs=bit0 */ '?',
    /* kb_3=bit1 */ 0,      /* theta - skip */
    /* kb_6=bit2 */ 'W',
    /* kb_9=bit3 */ 'T',
    /* kb_RParen=bit4 */ 'L',
    /* kb_Tan=bit5 */ 'G',
    /* kb_Vars=bit6 */ 'D',
    /* bit7 */ 0
};

static const char alpha_map_g6[] = {
    /* kb_Enter=bit0 */ 0,
    /* kb_Add=bit1 */ '"',
    /* kb_Sub=bit2 */ 'X',
    /* kb_Mul=bit3 */ 'P',
    /* kb_Div=bit4 */ 'I',
    /* kb_Power=bit5 */ 'H',
    /* kb_Clear=bit6 */ 0,
    /* bit7 */ 0
};

static const char alpha_map_g2[] = {
    /* bit0 */ 0,
    /* kb_Sto=bit1 */ 0,
    /* kb_Ln=bit2 */ 'O',
    /* kb_Log=bit3 */ 'N',
    /* kb_Square=bit4 */ 'M',
    /* kb_Recip=bit5 */ 'A',
    /* kb_Math=bit6 */ 'A',
    /* kb_Alpha=bit7 */ 0
};

/* Number keys (no alpha) */
static const char num_map_g3[] = {
    '0', '1', '4', '7', ',', 0, 0, 0
};
static const char num_map_g4[] = {
    '.', '2', '5', '8', '(', 0, 0, 0
};
static const char num_map_g5[] = {
    '-', '3', '6', '9', ')', 0, 0, 0
};
static const char num_map_g6[] = {
    0, '+', '-', '*', '/', '^', 0, 0
};

/* ── Screen helpers ── */
static int cursor_y = 0;

static void screen_clear(void) {
    gfx_FillScreen(COLOR_BG);
    cursor_y = 0;
}

static void print_line(const char *text, uint8_t color) {
    gfx_SetTextFGColor(color);
    gfx_PrintStringXY(text, 0, cursor_y);
    cursor_y += FONT_H;
    if (cursor_y >= SCREEN_H) {
        cursor_y = SCREEN_H - FONT_H;
    }
}

static void print_wrapped(const char *text, uint8_t color) {
    char line[CHARS_PER_LINE + 1];
    int len = (int)strlen(text);
    int pos = 0;

    gfx_SetTextFGColor(color);

    while (pos < len) {
        int chunk = len - pos;
        if (chunk > CHARS_PER_LINE) chunk = CHARS_PER_LINE;
        memcpy(line, text + pos, chunk);
        line[chunk] = '\0';
        gfx_PrintStringXY(line, 0, cursor_y);
        cursor_y += FONT_H;
        pos += chunk;

        if (cursor_y >= SCREEN_H) {
            /* Screen full — wait for keypress then clear */
            gfx_SetTextFGColor(COLOR_WAIT);
            gfx_PrintStringXY("[MORE - press any key]", 0, SCREEN_H - FONT_H);
            while (!kb_AnyKey()) { kb_Scan(); }
            while (kb_AnyKey()) { kb_Scan(); }  /* debounce */
            screen_clear();
        }
    }
}

static void draw_input_prompt(void) {
    char display[CHARS_PER_LINE + 1];
    int show_len;

    /* Clear input area (bottom 2 lines) */
    gfx_SetColor(COLOR_BG);
    gfx_FillRectangle_NoClip(0, SCREEN_H - FONT_H * 2, SCREEN_W, FONT_H * 2);

    /* Show "> " prefix + last N chars of input */
    gfx_SetTextFGColor(COLOR_PROMPT);
    gfx_PrintStringXY("> ", 0, SCREEN_H - FONT_H);

    show_len = input_len;
    if (show_len > CHARS_PER_LINE - 3) show_len = CHARS_PER_LINE - 3;
    memcpy(display, input_buf + (input_len - show_len), show_len);
    display[show_len] = '_';  /* cursor */
    display[show_len + 1] = '\0';

    gfx_SetTextFGColor(COLOR_FG);
    gfx_PrintStringXY(display, CHAR_W * 2, SCREEN_H - FONT_H);
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

/* ── Read a full line from serial (blocking with USB polling) ── */
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

/* ── Send string over serial ── */
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

    /* Send newline terminator */
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

/* ── Keyboard input handler ── */
static bool alpha_mode = false;
static bool prev_keys[8][8] = {{false}};

static char get_alpha_char(int group, int bit) {
    switch (group) {
        case 2: return (bit < 8) ? alpha_map_g2[bit] : 0;
        case 3: return (bit < 8) ? alpha_map_g3[bit] : 0;
        case 4: return (bit < 8) ? alpha_map_g4[bit] : 0;
        case 5: return (bit < 8) ? alpha_map_g5[bit] : 0;
        case 6: return (bit < 8) ? alpha_map_g6[bit] : 0;
        default: return 0;
    }
}

static char get_num_char(int group, int bit) {
    switch (group) {
        case 3: return (bit < 8) ? num_map_g3[bit] : 0;
        case 4: return (bit < 8) ? num_map_g4[bit] : 0;
        case 5: return (bit < 8) ? num_map_g5[bit] : 0;
        case 6: return (bit < 8) ? num_map_g6[bit] : 0;
        default: return 0;
    }
}

/* Returns: 0 = still typing, 1 = enter pressed, -1 = clear pressed */
static int handle_keypress(void) {
    bool curr_keys[8][8] = {{false}};

    kb_Scan();

    /* Build current key state */
    for (int g = 1; g <= 7; g++) {
        for (int b = 0; b < 8; b++) {
            curr_keys[g][b] = (kb_Data[g] & (1 << b)) != 0;
        }
    }

    /* Check for new key presses (edge detection) */
    for (int g = 1; g <= 7; g++) {
        for (int b = 0; b < 8; b++) {
            if (curr_keys[g][b] && !prev_keys[g][b]) {
                /* New keypress detected */

                /* ALPHA toggle */
                if (g == 2 && b == 7) {
                    alpha_mode = !alpha_mode;
                    goto done;
                }

                /* CLEAR = cancel */
                if (g == 6 && b == 6) {
                    memcpy(prev_keys, curr_keys, sizeof(prev_keys));
                    return -1;
                }

                /* ENTER = submit */
                if (g == 6 && b == 0) {
                    memcpy(prev_keys, curr_keys, sizeof(prev_keys));
                    return 1;
                }

                /* DEL = backspace */
                if (g == 1 && b == 7) {
                    if (input_len > 0) {
                        input_len--;
                        input_buf[input_len] = '\0';
                    }
                    goto done;
                }

                /* Character input */
                if (input_len < MAX_INPUT - 1) {
                    char c = 0;
                    if (alpha_mode) {
                        c = get_alpha_char(g, b);
                    }
                    if (c == 0) {
                        c = get_num_char(g, b);
                    }
                    if (c != 0) {
                        input_buf[input_len++] = c;
                        input_buf[input_len] = '\0';
                    }
                }
            }
        }
    }

done:
    memcpy(prev_keys, curr_keys, sizeof(prev_keys));
    return 0;
}

/* ── Main ── */
int main(void) {
    /* Init graphics */
    gfx_Begin();
    gfx_SetMonospaceFont(CHAR_W);
    gfx_SetTextBGColor(COLOR_BG);
    gfx_SetTextTransparentColor(COLOR_BG);

    screen_clear();
    print_line("AICHAT v1.0", COLOR_PROMPT);
    print_line("Connect USB and start bridge.py", COLOR_FG);
    print_line("ALPHA=toggle letters  DEL=backspace", COLOR_FG);
    print_line("ENTER=send  CLEAR=exit", COLOR_FG);
    print_line("", COLOR_FG);

    /* Init USB */
    const usb_standard_descriptors_t *desc = srl_GetCDCStandardDescriptors();
    usb_error_t usb_err = usb_Init(handle_usb_event, NULL, desc, USB_DEFAULT_INIT_FLAGS);
    if (usb_err) {
        print_line("USB init failed!", 0xE0);
        print_line("Press any key to exit.", COLOR_FG);
        while (!kb_AnyKey()) { kb_Scan(); }
        gfx_End();
        return 1;
    }

    /* Wait for USB connection */
    print_line("Waiting for USB connection...", COLOR_WAIT);
    while (!has_device) {
        kb_Scan();
        if (kb_IsDown(kb_KeyClear)) {
            usb_Cleanup();
            gfx_End();
            return 0;
        }
        usb_HandleEvents();
    }

    screen_clear();
    print_line("Connected! Type your question.", COLOR_WAIT);
    print_line("ALPHA=letters DEL=back ENTER=send", COLOR_FG);
    cursor_y = FONT_H * 3;

    /* Main chat loop */
    bool running = true;
    while (running) {
        /* Reset input */
        input_len = 0;
        input_buf[0] = '\0';
        alpha_mode = true;  /* start in alpha mode for text entry */
        draw_input_prompt();

        /* Collect input */
        int result;
        do {
            result = handle_keypress();
            if (result == -1) {
                running = false;
                break;
            }
            usb_HandleEvents();
            draw_input_prompt();

            /* Small delay for debounce */
            delay(50);
        } while (result == 0);

        if (!running || input_len == 0) continue;

        /* Show waiting message */
        screen_clear();
        print_line("You:", COLOR_PROMPT);
        print_wrapped(input_buf, COLOR_FG);
        cursor_y += FONT_H;
        print_line("Waiting for AI response...", COLOR_WAIT);

        /* Send query */
        if (!send_query(input_buf)) {
            print_line("Send failed! USB disconnected?", 0xE0);
            print_line("Press any key...", COLOR_FG);
            while (!kb_AnyKey()) { kb_Scan(); }
            while (kb_AnyKey()) { kb_Scan(); }
            continue;
        }

        /* Read response */
        if (!read_line(resp_buf, MAX_RESP)) {
            running = false;
            continue;
        }

        /* Display response */
        screen_clear();
        print_line("You:", COLOR_PROMPT);
        print_wrapped(input_buf, COLOR_FG);
        cursor_y += FONT_H;
        print_line("AI:", COLOR_PROMPT);
        print_wrapped(resp_buf, COLOR_FG);
        cursor_y += FONT_H;
        print_line("[Press any key for new query]", COLOR_WAIT);

        /* Wait for keypress */
        while (!kb_AnyKey()) {
            kb_Scan();
            usb_HandleEvents();
        }
        while (kb_AnyKey()) { kb_Scan(); }  /* debounce */
        screen_clear();
        cursor_y = FONT_H * 2;
    }

    usb_Cleanup();
    gfx_End();
    return 0;
}
