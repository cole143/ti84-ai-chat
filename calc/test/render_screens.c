/* Renders every ZACH GPT screen through the host shims into test/out/ as PPM files so the
 * layout can be checked without a calculator. Build: see test/render.sh */
#define main calc_main
#include "../src/main.c"
#undef main

#include <stdio.h>

static void save(const char *name) {
    char path[128];
    snprintf(path, sizeof path, "test/out/%s.ppm", name);
    if (shim_write_ppm(path) == 0) printf("wrote %s (oob writes so far: %d)\n", path, shim_oob);
}

int main(void) {
    const char *q = "WHAT IS THE SHARPE RATIO FOR A FUND WITH 12% RETURN 4% RF AND 10% S";
    const char *a = "Sharpe ratio = (Rp - Rf) / SD = (12% - 4%) / 10% = 0.80. It is the slope of the "
                    "capital allocation line: 0.80 units of excess return per unit of total risk. "
                    "Higher is better when comparing funds.";
    int i;

    shim_load_font("test/font8x8.bin");
    gfx_Begin();
    gfx_SetPalette(palette, sizeof palette, 0);
    gfx_SetMonospaceFont(CHAR_W);
    gfx_SetTextBGColor(C_TEXT_TP);
    gfx_SetTextTransparentColor(C_TEXT_TP);
    gfx_SetTextConfig(gfx_text_noclip);
    rain_init(rain, 7);
    t9_init(&t9);
    chat_init(&chat);

    /* 1. Splash, waiting (two dots) */
    screen = SCR_SPLASH; status = ST_WAITING; tick = 8;
    for (i = 0; i < 8; i++) rain_step(rain, (uint8_t)i);
    draw_current(); save("01_splash_waiting");

    /* 2. Splash, connected */
    status = ST_CONNECTED; tick = 13;
    for (i = 8; i < 13; i++) rain_step(rain, (uint8_t)i);
    draw_current(); save("02_splash_connected");

    /* 3. Typing, pending letter K on key 5 (second tap), key flashed */
    screen = SCR_TYPING;
    t9_set_text(&t9, q);
    t9_digit(&t9, 5, 0); t9_digit(&t9, 5, 100);
    flash_key = 5; tick = 0;
    draw_current(); save("03_typing_pending");

    /* 4. Typing, idle caret, 123 mode, short text */
    t9_init(&t9); t9_alpha(&t9); t9_set_text(&t9, "12%");
    flash_key = -1; tick = 0;
    draw_current(); save("04_typing_idle_123");

    /* 5. Typing, empty, ABC mode */
    t9_init(&t9);
    draw_current(); save("05_typing_empty");

    /* 6. Thinking */
    t9_init(&t9);
    chat_push(&chat, ROLE_USER, "WHAT IS THE SHARPE RATIO FOR A FUND WITH 12% RETURN 4% RF AND 10% SD");
    screen = SCR_THINKING; scroll = 0xFFFF; tick = 3;
    draw_current(); save("06_thinking");

    /* 7. Answer, scrolled to bottom */
    chat_push(&chat, ROLE_AI, a);
    screen = SCR_ANSWER; scroll = 0xFFFF;
    draw_current(); save("07_answer");

    /* 8. Answer with history (6 messages), scrolled to the top */
    chat_push(&chat, ROLE_USER, "AND BETA?");
    chat_push(&chat, ROLE_AI, "Beta measures sensitivity to the market. Beta = Cov(Ri, Rm) / Var(Rm). "
                               "Beta 1.2 means the asset moves about 20% more than the market in either direction.");
    chat_push(&chat, ROLE_USER, "WHAT ABOUT DURATION OF A BOND WITH 6 YEARS");
    chat_push(&chat, ROLE_AI, "Modified duration approximates a bond's price sensitivity: %dP = -D * dY. "
                               "A duration of 6 means a 1% rise in yield cuts price by about 6%.");
    scroll = 0xFFFF; draw_current(); save("08_answer_history_bottom");
    scroll = 0;      draw_current(); save("09_answer_history_top");
    scroll = 60;     draw_current(); save("10_answer_history_mid");

    printf("done; out-of-bounds NoClip writes: %d\n", shim_oob);
    return shim_oob ? 1 : 0;
}
