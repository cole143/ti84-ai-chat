/*
 * AICHAT v3.0 — TI-84 Plus CE AI Chat Bridge
 * On-screen QWERTY keyboard + autocomplete from course word bank.
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
#define MAX_INPUT   512
#define MAX_RESP    512
#define SRL_BUF_SZ  4096

#define SCREEN_W    320
#define SCREEN_H    240
#define FONT_H      8
#define CHAR_W      8
#define CHARS_PER_LINE (SCREEN_W / CHAR_W)

/* Colors */
#define COLOR_BG      0xFF
#define COLOR_FG      0x00
#define COLOR_PROMPT  0x10
#define COLOR_WAIT    0x03
#define COLOR_KEY_BG  0xDE
#define COLOR_KEY_HL  0x10
#define COLOR_KEY_FG  0x00
#define COLOR_KEY_HFG 0xFF
#define COLOR_DIVIDER 0xB5
#define COLOR_AC_BG   0xC6   /* suggestion background - lighter blue-gray */
#define COLOR_AC_NUM  0x10   /* suggestion number - dark blue */

/* ── Autocomplete word bank ── */
/* Sorted by frequency (highest first). Max 200 words. */
#define WORDBANK_SIZE 200
#define MAX_WORD_LEN  16
#define MAX_SUGGESTIONS 3

static const char wordbank[WORDBANK_SIZE][MAX_WORD_LEN] = {
    "THE","WHAT","WHICH","WHEN","WHERE","HOW","THAT","THIS","WITH","FROM",
    "DOES","HAVE","WILL","WOULD","BETWEEN","FOLLOWING","BLANK","ABOVE","BELOW","LEFT",
    "RIGHT","AND","NOT","CAN","ARE","IS","OF","IN","FOR","TO",
    "BY","AT","AN","ON","OR","IF","IT","AS","ALL","BUT",
    "DO","NO","SO","UP","BE","RISK","MARKET","RETURN","BOND","PORTFOLIO",
    "RATE","YIELD","PRICE","CAPITAL","ASSET","CAPM","DURATION","BETA","ALPHA","PREMIUM",
    "STANDARD","DEVIATION","MATURITY","STOCK","EXCESS","DIVERSIFICATION","EFFICIENT","SHARPE",
    "CORRELATION","COVARIANCE","EXPECTED","SYSTEMATIC","FRONTIER","ALLOCATION","IMMUNIZATION",
    "EMH","COUPON","VARIANCE","INVESTOR","INVESTMENT","TREASURY","PASSIVE","ACTIVE",
    "ARBITRAGE","INFLATION","DISCOUNT","LIQUIDITY","SECURITY","MODIFIED","MACAULAY","CONVEXITY",
    "CALLABLE","SEMISTRONG","INDENTURE","SENSITIVITY","REINVESTMENT","MUNICIPAL","CORPORATE",
    "DIVIDEND","FLOATING","SINKING","COLLATERAL","NOMINAL","EQUILIBRIUM","MOMENTUM","RANDOM",
    "WALK","TECHNICAL","FUNDAMENTAL","PUTTABLE","CONVERTIBLE","GEOMETRIC","ARITHMETIC","KURTOSIS",
    "ANOMALY","AVERSION","EQUITY","HYPOTHESIS","CURVE","FUND","MUTUAL","PERIOD",
    "HOLDING","PROBABILITY","NORMAL","DISTRIBUTION","WEIGHTED","AVERAGE","HORIZON","SWAP",
    "SPREAD","EARNINGS","LEVERAGE","DEBT","LOSS","GAINS","TAX","CREDIT",
    "RATING","SPECULATIVE","VOLATILITY","FORECAST","ANALYSIS","MANAGEMENT","GROWTH","AGGREGATE",
    "MULTIFACTOR","SCENARIO","COMPOUNDING","LIABILITIES","PROFIT","PRINCIPAL","BILLS","NOTES",
    "PAR","ISSUER","SML","CAL","APT","HPR","YTM","IRR",
    "NPV","EAR","APR","TVM","VAR","REBALANCING","NONSYSTEMATIC","REINVESTED",
    "MANAGERS","INCOME","SHARES","PROFITABILITY","SOLVENCY","COVERAGE","TAXES","LOSSES",
    "RISKLESS","MISPRICED","REGRESSION","INFORMATION","DEBENTURE","ACCRUED","SKEWNESS","BORROWING",
    "RECESSION","SLOPE","CATASTROPHE","AMORTIZE","FIXED","SHORT","LONG","NEGATIVE",
    "POSITIVE","ZERO","SMALL","LARGE","TOTAL","MEAN"
};

/* ── Phrase bank for phrase mode ── */
#define MAX_PHRASE_LEN   40
#define NUM_CATEGORIES    4
#define MAX_PHRASES_PER_CAT 8

static const char *cat_names[NUM_CATEGORIES] = {
    "STARTERS", "QUALIFIERS", "RATIOS", "FINANCE"
};

static const char phrases[NUM_CATEGORIES][MAX_PHRASES_PER_CAT][MAX_PHRASE_LEN] = {
    /* STARTERS */
    {
        "WHICH OF THE FOLLOWING",
        "A COMPANY HAS",
        "A COMPANY",
        "AN ANALYST",
        "SHOULD A COMPANY",
        "WHAT HAPPENS TO THE",
        "HOW MUCH",
        ""
    },
    /* QUALIFIERS */
    {
        "MOST LIKELY",
        "LEAST LIKELY",
        "MOST APPROPRIATE",
        "CLOSEST TO",
        "BEST DESCRIBES",
        "SHOULD BE CONSIDERED",
        "WITH RESPECT TO",
        "IN ORDER TO"
    },
    /* RATIOS */
    {
        "CURRENT RATIO",
        "QUICK RATIO",
        "CASH CONVERSION CYCLE",
        "NET PROFIT MARGIN",
        "CURRENT ASSETS",
        "CURRENT LIABILITIES",
        "ACCOUNTS PAYABLE",
        ""
    },
    /* FINANCE */
    {
        "WEIGHTED AVERAGE COST OF CAPITAL",
        "COST OF CAPITAL",
        "NET PRESENT VALUE",
        "DISCOUNTED CASH FLOW",
        "CAPITAL INVESTMENTS",
        "CAPITAL BUDGETING",
        "CORPORATE GOVERNANCE",
        "FINANCIAL REPORTING QUALITY"
    }
};

/* Count non-empty phrases in a category */
static int cat_phrase_count(int cat) {
    int count = 0;
    for (int i = 0; i < MAX_PHRASES_PER_CAT; i++) {
        if (phrases[cat][i][0] != '\0') count++;
        else break;
    }
    return count;
}

/* ── Globals (declared early for autocomplete) ── */
static srl_device_t srl;
static bool has_device = false;
static uint8_t srl_buf[SRL_BUF_SZ];
static char input_buf[MAX_INPUT];
static int input_len = 0;
static char resp_buf[MAX_RESP];
static int text_y = 0;

/* Autocomplete state */
static char suggestions[MAX_SUGGESTIONS][MAX_WORD_LEN];
static int num_suggestions = 0;

/* Get the start of the current (incomplete) word being typed */
static int get_current_word_start(void) {
    int i = input_len - 1;
    while (i >= 0 && input_buf[i] != ' ')
        i--;
    return i + 1;
}

/* Find up to 3 suggestions matching the current word prefix */
static void update_suggestions(void) {
    num_suggestions = 0;
    int word_start = get_current_word_start();
    int prefix_len = input_len - word_start;

    /* Only suggest after 2+ characters */
    if (prefix_len < 2) return;

    const char *prefix = input_buf + word_start;

    for (int i = 0; i < WORDBANK_SIZE && num_suggestions < MAX_SUGGESTIONS; i++) {
        const char *word = wordbank[i];
        int wlen = (int)strlen(word);
        if (wlen <= prefix_len) continue;  /* skip words same length or shorter */

        /* Check prefix match */
        bool match = true;
        for (int j = 0; j < prefix_len; j++) {
            if (prefix[j] != word[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            strncpy(suggestions[num_suggestions], word, MAX_WORD_LEN - 1);
            suggestions[num_suggestions][MAX_WORD_LEN - 1] = '\0';
            num_suggestions++;
        }
    }
}

/* Accept a suggestion: replace current partial word with full word + space */
static void accept_suggestion(int idx) {
    if (idx < 0 || idx >= num_suggestions) return;

    int word_start = get_current_word_start();
    const char *word = suggestions[idx];
    int wlen = (int)strlen(word);

    /* Check if it fits */
    if (word_start + wlen + 1 >= MAX_INPUT) return;

    /* Replace from word_start */
    memcpy(input_buf + word_start, word, wlen);
    input_buf[word_start + wlen] = ' ';
    input_len = word_start + wlen + 1;
    input_buf[input_len] = '\0';
    num_suggestions = 0;
}

/* ── Keyboard layout ── */
#define KB_TOP_Y      112
#define KB_KEY_W      28
#define KB_KEY_H      22
#define KB_PAD        2
#define KB_ROWS       5
#define SUGGEST_Y     (KB_TOP_Y - 14)

#define NUM_KEY_W     24    /* narrower keys for 12-char number row */
#define ROW_NUM_LEN   12
#define ROW0_LEN  10
#define ROW1_LEN  9
#define ROW2_LEN  7
#define ROW3_LEN  3

static const char row_num_chars[] = "1234567890%.";
static const char row0_chars[] = "QWERTYUIOP";
static const char row1_chars[] = "ASDFGHJKL";
static const char row2_chars[] = "ZXCVBNM";
static const int row_lens[] = {ROW_NUM_LEN, ROW0_LEN, ROW1_LEN, ROW2_LEN, ROW3_LEN};

static int kb_row = 0;
static int kb_col = 0;

static int row_x_offset(int row) {
    switch (row) {
        case 0: return (SCREEN_W - ROW_NUM_LEN * (NUM_KEY_W + KB_PAD)) / 2;
        case 1: return (SCREEN_W - ROW0_LEN * (KB_KEY_W + KB_PAD)) / 2;
        case 2: return (SCREEN_W - ROW1_LEN * (KB_KEY_W + KB_PAD)) / 2;
        case 3: return (SCREEN_W - ROW2_LEN * (KB_KEY_W + KB_PAD)) / 2;
        case 4: return 4;
        default: return 0;
    }
}

/* (globals declared above with autocomplete) */

/* ── Draw helpers ── */
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

static void draw_input_line(void) {
    char display[CHARS_PER_LINE + 1];
    int show_len;

    gfx_SetColor(COLOR_BG);
    gfx_FillRectangle_NoClip(0, 0, SCREEN_W, FONT_H * 2);

    gfx_SetTextFGColor(COLOR_PROMPT);
    gfx_PrintStringXY("> ", 0, 0);

    show_len = input_len;
    if (show_len > CHARS_PER_LINE - 3) show_len = CHARS_PER_LINE - 3;
    memcpy(display, input_buf + (input_len - show_len), show_len);
    display[show_len] = '_';
    display[show_len + 1] = '\0';

    gfx_SetTextFGColor(COLOR_FG);
    gfx_PrintStringXY(display, CHAR_W * 2, 0);
}

/* Draw suggestion bar */
static void draw_suggestions(void) {
    /* Clear suggestion area */
    gfx_SetColor(COLOR_BG);
    gfx_FillRectangle_NoClip(0, SUGGEST_Y - 2, SCREEN_W, 14);

    if (num_suggestions == 0) return;

    int x = 4;
    char label[MAX_WORD_LEN + 4];

    for (int i = 0; i < num_suggestions; i++) {
        int wlen = (int)strlen(suggestions[i]);
        int box_w = (wlen + 3) * CHAR_W + 4;  /* "[N]WORD" + padding */

        /* Background */
        gfx_SetColor(COLOR_AC_BG);
        gfx_FillRectangle_NoClip(x, SUGGEST_Y, box_w, 12);

        /* "[N]" */
        snprintf(label, sizeof(label), "[%d]%s", i + 1, suggestions[i]);
        gfx_SetTextFGColor(COLOR_AC_NUM);
        gfx_PrintStringXY(label, x + 2, SUGGEST_Y + 2);

        x += box_w + 6;
        if (x > SCREEN_W - 40) break;
    }
}

/* ── On-screen keyboard ── */
static void draw_keyboard(void) {
    char ch_str[2] = {0, 0};

    gfx_SetColor(COLOR_DIVIDER);
    gfx_HorizLine_NoClip(0, KB_TOP_Y - 2, SCREEN_W);

    /* Number row (row 0) — narrower keys */
    {
        int x_off = row_x_offset(0);
        int y = KB_TOP_Y;
        for (int c = 0; c < ROW_NUM_LEN; c++) {
            int x = x_off + c * (NUM_KEY_W + KB_PAD);
            bool highlighted = (kb_row == 0 && c == kb_col);

            gfx_SetColor(highlighted ? COLOR_KEY_HL : COLOR_KEY_BG);
            gfx_FillRectangle_NoClip(x, y, NUM_KEY_W, KB_KEY_H);

            ch_str[0] = row_num_chars[c];
            int tx = x + (NUM_KEY_W - CHAR_W) / 2;
            int ty = y + (KB_KEY_H - FONT_H) / 2;
            gfx_SetTextFGColor(highlighted ? COLOR_KEY_HFG : COLOR_KEY_FG);
            gfx_PrintStringXY(ch_str, tx, ty);
        }
    }

    /* Letter rows (rows 1-3) */
    for (int r = 1; r <= 3; r++) {
        int x_off = row_x_offset(r);
        int y = KB_TOP_Y + r * (KB_KEY_H + KB_PAD);
        int len = row_lens[r];
        const char *chars;

        switch (r) {
            case 1: chars = row0_chars; break;
            case 2: chars = row1_chars; break;
            default: chars = row2_chars; break;
        }

        for (int c = 0; c < len; c++) {
            int x = x_off + c * (KB_KEY_W + KB_PAD);
            bool highlighted = (r == kb_row && c == kb_col);

            gfx_SetColor(highlighted ? COLOR_KEY_HL : COLOR_KEY_BG);
            gfx_FillRectangle_NoClip(x, y, KB_KEY_W, KB_KEY_H);

            ch_str[0] = chars[c];
            int tx = x + (KB_KEY_W - CHAR_W) / 2;
            int ty = y + (KB_KEY_H - FONT_H) / 2;
            gfx_SetTextFGColor(highlighted ? COLOR_KEY_HFG : COLOR_KEY_FG);
            gfx_PrintStringXY(ch_str, tx, ty);
        }
    }

    /* Control row (row 4) */
    {
        int y = KB_TOP_Y + 4 * (KB_KEY_H + KB_PAD);
        int space_w = 160, del_w = 60, send_w = 70, gap = 8;
        int x = 4;

        bool hl = (kb_row == 4 && kb_col == 0);
        gfx_SetColor(hl ? COLOR_KEY_HL : COLOR_KEY_BG);
        gfx_FillRectangle_NoClip(x, y, space_w, KB_KEY_H);
        gfx_SetTextFGColor(hl ? COLOR_KEY_HFG : COLOR_KEY_FG);
        gfx_PrintStringXY("SPACE", x + (space_w - 5 * CHAR_W) / 2, y + (KB_KEY_H - FONT_H) / 2);
        x += space_w + gap;

        hl = (kb_row == 4 && kb_col == 1);
        gfx_SetColor(hl ? COLOR_KEY_HL : COLOR_KEY_BG);
        gfx_FillRectangle_NoClip(x, y, del_w, KB_KEY_H);
        gfx_SetTextFGColor(hl ? COLOR_KEY_HFG : COLOR_KEY_FG);
        gfx_PrintStringXY("DEL", x + (del_w - 3 * CHAR_W) / 2, y + (KB_KEY_H - FONT_H) / 2);
        x += del_w + gap;

        hl = (kb_row == 4 && kb_col == 2);
        gfx_SetColor(hl ? COLOR_KEY_HL : COLOR_KEY_BG);
        gfx_FillRectangle_NoClip(x, y, send_w, KB_KEY_H);
        gfx_SetTextFGColor(hl ? COLOR_KEY_HFG : COLOR_KEY_FG);
        gfx_PrintStringXY("SEND", x + (send_w - 4 * CHAR_W) / 2, y + (KB_KEY_H - FONT_H) / 2);
    }
}

/* ── Phrase mode state ── */
static bool phrase_mode = false;
static int phrase_cat = 0;      /* current category tab (0-3) */
static int phrase_sel = 0;      /* selected phrase within category */

/* Draw the phrase mode overlay (full screen) */
static void draw_phrase_mode(void) {
    gfx_FillScreen(COLOR_BG);

    /* Title bar */
    gfx_SetColor(COLOR_KEY_HL);
    gfx_FillRectangle_NoClip(0, 0, SCREEN_W, 14);
    gfx_SetTextFGColor(COLOR_KEY_HFG);
    gfx_PrintStringXY("PHRASE MODE  [MODE=cancel]", 4, 3);

    /* Draw input preview (line 2) */
    gfx_SetColor(COLOR_BG);
    gfx_SetTextFGColor(COLOR_PROMPT);
    gfx_PrintStringXY("> ", 0, 18);
    {
        char preview[CHARS_PER_LINE - 1];
        int show = input_len;
        if (show > CHARS_PER_LINE - 3) show = CHARS_PER_LINE - 3;
        memcpy(preview, input_buf + (input_len - show), show);
        preview[show] = '_';
        preview[show + 1] = '\0';
        gfx_SetTextFGColor(COLOR_FG);
        gfx_PrintStringXY(preview, CHAR_W * 2, 18);
    }

    /* Tab bar at y=32 */
    {
        int tab_w = SCREEN_W / NUM_CATEGORIES;
        for (int i = 0; i < NUM_CATEGORIES; i++) {
            int x = i * tab_w;
            bool active = (i == phrase_cat);
            gfx_SetColor(active ? COLOR_KEY_HL : COLOR_KEY_BG);
            gfx_FillRectangle_NoClip(x, 32, tab_w - 1, 16);
            gfx_SetTextFGColor(active ? COLOR_KEY_HFG : COLOR_KEY_FG);
            /* Center the tab name */
            int name_len = (int)strlen(cat_names[i]);
            int tx = x + (tab_w - name_len * CHAR_W) / 2;
            gfx_PrintStringXY(cat_names[i], tx, 36);
        }
    }

    /* Divider line */
    gfx_SetColor(COLOR_DIVIDER);
    gfx_HorizLine_NoClip(0, 50, SCREEN_W);

    /* Phrase list starting at y=54 */
    int count = cat_phrase_count(phrase_cat);
    for (int i = 0; i < count; i++) {
        int y = 54 + i * 20;
        if (y + 18 > SCREEN_H) break;  /* don't draw off screen */

        bool selected = (i == phrase_sel);

        /* Highlight bar for selected phrase */
        if (selected) {
            gfx_SetColor(COLOR_KEY_HL);
            gfx_FillRectangle_NoClip(2, y, SCREEN_W - 4, 18);
        }

        /* Phrase text */
        gfx_SetTextFGColor(selected ? COLOR_KEY_HFG : COLOR_FG);
        gfx_PrintStringXY(phrases[phrase_cat][i], 8, y + 5);
    }

    /* Bottom hint */
    gfx_SetTextFGColor(COLOR_WAIT);
    gfx_PrintStringXY("L/R=tab  U/D=select  ENTER=insert", 4, SCREEN_H - 12);
}

/* Handle input while in phrase mode.
 * Returns: 0 = still in phrase mode, 1 = phrase inserted (exit), -1 = cancelled (exit) */
static int handle_phrase_input(void) {
    static bool prev_up = false, prev_down = false;
    static bool prev_left = false, prev_right = false;
    static bool prev_enter = false, prev_mode = false;

    kb_Scan();

    bool up    = kb_IsDown(kb_KeyUp);
    bool down  = kb_IsDown(kb_KeyDown);
    bool left  = kb_IsDown(kb_KeyLeft);
    bool right = kb_IsDown(kb_KeyRight);
    bool enter = kb_IsDown(kb_KeyEnter);
    bool mode  = (kb_Data[1] & kb_Mode) != 0;

    /* MODE key cancels phrase mode */
    if (mode && !prev_mode) {
        prev_up = up; prev_down = down; prev_left = left; prev_right = right;
        prev_enter = enter; prev_mode = mode;
        return -1;
    }

    /* Left/Right: switch category tabs */
    if (left && !prev_left) {
        phrase_cat--;
        if (phrase_cat < 0) phrase_cat = NUM_CATEGORIES - 1;
        phrase_sel = 0;
    }
    if (right && !prev_right) {
        phrase_cat++;
        if (phrase_cat >= NUM_CATEGORIES) phrase_cat = 0;
        phrase_sel = 0;
    }

    /* Up/Down: navigate phrases within category */
    if (up && !prev_up) {
        phrase_sel--;
        if (phrase_sel < 0) phrase_sel = cat_phrase_count(phrase_cat) - 1;
    }
    if (down && !prev_down) {
        phrase_sel++;
        if (phrase_sel >= cat_phrase_count(phrase_cat)) phrase_sel = 0;
    }

    /* ENTER: insert selected phrase into input buffer */
    if (enter && !prev_enter) {
        const char *phrase = phrases[phrase_cat][phrase_sel];
        int plen = (int)strlen(phrase);

        /* Check if it fits (phrase + space) */
        if (input_len + plen + 1 < MAX_INPUT) {
            memcpy(input_buf + input_len, phrase, plen);
            input_len += plen;
            input_buf[input_len++] = ' ';
            input_buf[input_len] = '\0';
        }

        prev_up = up; prev_down = down; prev_left = left; prev_right = right;
        prev_enter = enter; prev_mode = mode;
        return 1;
    }

    prev_up = up; prev_down = down; prev_left = left; prev_right = right;
    prev_enter = enter; prev_mode = mode;
    return 0;
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
        if (!has_device) return false;
        char tmp[64];
        int n = srl_Read(&srl, tmp, sizeof tmp);
        if (n <= 0) continue;
        for (int i = 0; i < n && pos < max_len - 1; i++) {
            if (tmp[i] == '\n') { buf[pos] = '\0'; return true; }
            if (tmp[i] >= 0x20) { buf[pos++] = tmp[i]; }
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

/* ── Keyboard input ── */
static void clamp_col(void) {
    if (kb_col >= row_lens[kb_row]) kb_col = row_lens[kb_row] - 1;
    if (kb_col < 0) kb_col = 0;
}

/* Returns: 0 = still typing, 1 = send, -1 = exit */
static int handle_keyboard_input(void) {
    static bool prev_up = false, prev_down = false;
    static bool prev_left = false, prev_right = false;
    static bool prev_enter = false, prev_clear = false;
    static bool prev_del = false, prev_zero = false;
    static bool prev_1 = false, prev_2 = false, prev_3 = false;

    kb_Scan();

    bool up    = kb_IsDown(kb_KeyUp);
    bool down  = kb_IsDown(kb_KeyDown);
    bool left  = kb_IsDown(kb_KeyLeft);
    bool right = kb_IsDown(kb_KeyRight);
    bool enter = kb_IsDown(kb_KeyEnter);
    bool clear = kb_IsDown(kb_KeyClear);
    bool del   = kb_IsDown(kb_KeyDel);
    bool zero  = (kb_Data[3] & kb_0) != 0;
    bool key1  = (kb_Data[3] & kb_1) != 0;
    bool key2  = (kb_Data[4] & kb_2) != 0;
    bool key3  = (kb_Data[5] & kb_3) != 0;

    /* Autocomplete: press 1, 2, or 3 to accept suggestion */
    if (key1 && !prev_1 && num_suggestions >= 1) {
        accept_suggestion(0);
        prev_1 = key1; prev_2 = key2; prev_3 = key3;
        prev_up = up; prev_down = down; prev_left = left; prev_right = right;
        prev_enter = enter; prev_clear = clear; prev_del = del; prev_zero = zero;
        return 0;
    }
    if (key2 && !prev_2 && num_suggestions >= 2) {
        accept_suggestion(1);
        prev_1 = key1; prev_2 = key2; prev_3 = key3;
        prev_up = up; prev_down = down; prev_left = left; prev_right = right;
        prev_enter = enter; prev_clear = clear; prev_del = del; prev_zero = zero;
        return 0;
    }
    if (key3 && !prev_3 && num_suggestions >= 3) {
        accept_suggestion(2);
        prev_1 = key1; prev_2 = key2; prev_3 = key3;
        prev_up = up; prev_down = down; prev_left = left; prev_right = right;
        prev_enter = enter; prev_clear = clear; prev_del = del; prev_zero = zero;
        return 0;
    }

    /* Arrow navigation */
    if (up && !prev_up) { kb_row--; if (kb_row < 0) kb_row = KB_ROWS - 1; clamp_col(); }
    if (down && !prev_down) { kb_row++; if (kb_row >= KB_ROWS) kb_row = 0; clamp_col(); }
    if (left && !prev_left) {
        kb_col--;
        if (kb_col < 0) { kb_row--; if (kb_row < 0) kb_row = KB_ROWS - 1; kb_col = row_lens[kb_row] - 1; }
    }
    if (right && !prev_right) {
        kb_col++;
        if (kb_col >= row_lens[kb_row]) { kb_row++; if (kb_row >= KB_ROWS) kb_row = 0; kb_col = 0; }
    }

    if (clear && !prev_clear) {
        prev_1 = key1; prev_2 = key2; prev_3 = key3;
        prev_up = up; prev_down = down; prev_left = left; prev_right = right;
        prev_enter = enter; prev_clear = clear; prev_del = del; prev_zero = zero;
        return -1;
    }

    if (del && !prev_del) {
        if (input_len > 0) { input_len--; input_buf[input_len] = '\0'; }
    }

    if (zero && !prev_zero) {
        if (input_len < MAX_INPUT - 1) { input_buf[input_len++] = ' '; input_buf[input_len] = '\0'; }
    }

    if (enter && !prev_enter) {
        if (kb_row == 4) {
            if (kb_col == 0) {
                if (input_len < MAX_INPUT - 1) { input_buf[input_len++] = ' '; input_buf[input_len] = '\0'; }
            } else if (kb_col == 1) {
                if (input_len > 0) { input_len--; input_buf[input_len] = '\0'; }
            } else if (kb_col == 2) {
                prev_1 = key1; prev_2 = key2; prev_3 = key3;
                prev_up = up; prev_down = down; prev_left = left; prev_right = right;
                prev_enter = enter; prev_clear = clear; prev_del = del; prev_zero = zero;
                return 1;
            }
        } else {
            char c;
            switch (kb_row) {
                case 0: c = row_num_chars[kb_col]; break;
                case 1: c = row0_chars[kb_col]; break;
                case 2: c = row1_chars[kb_col]; break;
                case 3: c = row2_chars[kb_col]; break;
                default: c = 0; break;
            }
            if (c != 0 && input_len < MAX_INPUT - 1) {
                input_buf[input_len++] = c;
                input_buf[input_len] = '\0';
            }
        }
    }

    prev_1 = key1; prev_2 = key2; prev_3 = key3;
    prev_up = up; prev_down = down; prev_left = left; prev_right = right;
    prev_enter = enter; prev_clear = clear; prev_del = del; prev_zero = zero;
    return 0;
}

/* ── Main ── */
int main(void) {
    gfx_Begin();
    gfx_SetMonospaceFont(CHAR_W);
    gfx_SetTextBGColor(COLOR_BG);
    gfx_SetTextTransparentColor(0xFE);
    gfx_FillScreen(COLOR_BG);

    text_y = 0;
    print_top_line("AICHAT v3.1", COLOR_PROMPT);
    print_top_line("Connect USB and start bridge.py", COLOR_FG);
    print_top_line("Arrows=move ENTER=type 1/2/3=autofill", COLOR_FG);

    const usb_standard_descriptors_t *desc = srl_GetCDCStandardDescriptors();
    usb_error_t usb_err = usb_Init(handle_usb_event, NULL, desc, USB_DEFAULT_INIT_FLAGS);
    if (usb_err) {
        print_top_line("USB init failed!", 0xE0);
        while (!kb_AnyKey()) { kb_Scan(); }
        gfx_End();
        return 1;
    }

    print_top_line("Waiting for USB connection...", COLOR_WAIT);
    while (!has_device) {
        kb_Scan();
        if (kb_IsDown(kb_KeyClear)) { usb_Cleanup(); gfx_End(); return 0; }
        usb_HandleEvents();
    }

    bool running = true;
    while (running) {
        /* If USB disconnected, wait for reconnect */
        if (!has_device) {
            gfx_FillScreen(COLOR_BG);
            text_y = 0;
            print_top_line("USB disconnected.", 0xE0);
            print_top_line("Reconnect cable and wait...", COLOR_WAIT);
            while (!has_device) {
                kb_Scan();
                if (kb_IsDown(kb_KeyClear)) { running = false; break; }
                usb_HandleEvents();
            }
            if (!running) continue;
            gfx_FillScreen(COLOR_BG);
            text_y = 0;
            print_top_line("Reconnected!", COLOR_PROMPT);
            delay(500);
        }

        input_len = 0;
        input_buf[0] = '\0';
        num_suggestions = 0;
        kb_row = 0;
        kb_col = 0;

        gfx_FillScreen(COLOR_BG);
        draw_input_line();
        draw_suggestions();
        draw_keyboard();

        int result;
        do {
            result = handle_keyboard_input();
            if (result == -1) { running = false; break; }
            usb_HandleEvents();
            if (!has_device) break;  /* drop to reconnect screen */
            update_suggestions();
            draw_input_line();
            draw_suggestions();
            draw_keyboard();
            delay(80);
        } while (result == 0);

        if (!running || !has_device || input_len == 0) continue;

        gfx_FillScreen(COLOR_BG);
        text_y = 0;
        print_top_line("You:", COLOR_PROMPT);
        print_top_wrapped(input_buf, COLOR_FG, KB_TOP_Y - 10);
        text_y += FONT_H;
        print_top_line("Waiting for AI response...", COLOR_WAIT);

        if (!send_query(input_buf)) {
            if (!has_device) continue;  /* drop to reconnect screen */
            print_top_line("Send failed!", 0xE0);
            while (!kb_AnyKey()) { kb_Scan(); }
            while (kb_AnyKey()) { kb_Scan(); }
            continue;
        }

        if (!read_line(resp_buf, MAX_RESP)) {
            if (!has_device) continue;  /* drop to reconnect screen */
            running = false;
            continue;
        }

        gfx_FillScreen(COLOR_BG);
        text_y = 0;
        print_top_line("You:", COLOR_PROMPT);
        print_top_wrapped(input_buf, COLOR_FG, SCREEN_H);
        text_y += FONT_H;
        print_top_line("AI:", COLOR_PROMPT);
        print_top_wrapped(resp_buf, COLOR_FG, SCREEN_H - FONT_H);
        gfx_SetTextFGColor(COLOR_WAIT);
        gfx_PrintStringXY("[Any key -> new query]", 0, SCREEN_H - FONT_H);

        while (!kb_AnyKey()) { kb_Scan(); usb_HandleEvents(); }
        while (kb_AnyKey()) { kb_Scan(); }
    }

    usb_Cleanup();
    gfx_End();
    return 0;
}
