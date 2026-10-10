/* Host implementation of the graphx/keypadc/usb shims: a 320x240 paletted
 * framebuffer with graphx-like text (transparent BG, clip mode ignores scale,
 * NoClip calls are bounds-checked and counted instead of corrupting memory). */
#include <stdio.h>
#include <string.h>
#include "graphx.h"
#include "keypadc.h"
#include "usbdrvce.h"
#include "srldrvce.h"

uint8_t  shim_fb[240][320];
uint16_t shim_pal[256];
int      shim_oob = 0;

static uint8_t font[128][8];
static bool    have_font = false;
static uint8_t color = 0, text_fg = 0, text_bg = 255, text_tp = 255, scale_w = 1, scale_h = 1, text_clip = 0;
static int     text_x = 0, text_y = 0;
static int     clip_x0 = 0, clip_y0 = 0, clip_x1 = 320, clip_y1 = 240;

void shim_load_font(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f && fread(font, 1, sizeof font, f) == sizeof font) have_font = true;
    if (f) fclose(f);
    if (!have_font) {                      /* fallback: solid 6x7 box per glyph */
        int c, r;
        for (c = 0; c < 128; c++) for (r = 0; r < 8; r++) font[c][r] = (c > 0x20 && r < 7) ? 0x7C : 0;
    }
}

void gfx_Begin(void) { memset(shim_fb, 0, sizeof shim_fb); }
void gfx_End(void) {}
void gfx_SetPalette(const void *p, uint24_t size, uint24_t offset) { memcpy(&shim_pal[offset], p, size); }
void gfx_SetMonospaceFont(uint8_t s) { (void)s; }
void gfx_SetTextBGColor(uint8_t c) { text_bg = c; }
void gfx_SetTextTransparentColor(uint8_t c) { text_tp = c; }
void gfx_SetTextConfig(uint8_t cfg) { text_clip = (cfg == gfx_text_clip); }
void gfx_SetDrawBuffer(void) {}
void gfx_SetDrawScreen(void) {}
void gfx_SwapDraw(void) {}
void gfx_FillScreen(uint8_t c) { memset(shim_fb, c, sizeof shim_fb); }
void gfx_SetColor(uint8_t c) { color = c; }
void gfx_SetTextFGColor(uint8_t c) { text_fg = c; }
void gfx_SetTextXY(int x, int y) { text_x = x; text_y = y; }
void gfx_SetTextScale(uint8_t w, uint8_t h) { scale_w = w; scale_h = h; }
void gfx_SetClipRegion(int x0, int y0, int x1, int y1) { clip_x0 = x0; clip_y0 = y0; clip_x1 = x1; clip_y1 = y1; }

static void put_clipped(int x, int y, uint8_t c) {
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1) return;
    shim_fb[y][x] = c;
}
static void put_noclip(int x, int y, uint8_t c) {
    if (x < 0 || x >= 320 || y < 0 || y >= 240) { shim_oob++; return; }
    shim_fb[y][x] = c;
}

void gfx_FillRectangle(int x, int y, int w, int h) {
    int i, j;
    for (j = y; j < y + h; j++) for (i = x; i < x + w; i++) put_clipped(i, j, color);
}
void gfx_FillRectangle_NoClip(uint24_t x, uint8_t y, uint24_t w, uint8_t h) {
    unsigned i, j;
    for (j = y; j < (unsigned)y + h; j++) for (i = x; i < x + w; i++) put_noclip((int)i, (int)j, color);
}
void gfx_HorizLine_NoClip(uint24_t x, uint8_t y, uint24_t len) {
    unsigned i;
    for (i = x; i < x + len; i++) put_noclip((int)i, y, color);
}

void gfx_PrintChar(char ch) {
    unsigned c = (unsigned char)ch & 0x7F;
    int sw = text_clip ? 1 : scale_w, sh = text_clip ? 1 : scale_h;   /* graphx: clipped text ignores scale */
    int r, b, i, j;
    for (r = 0; r < 8; r++) {
        for (b = 0; b < 8; b++) {
            bool on = (font[c][r] >> (7 - b)) & 1;
            uint8_t col = on ? text_fg : text_bg;
            if (col == text_tp) continue;
            for (j = 0; j < sh; j++) for (i = 0; i < sw; i++) {
                int px = text_x + b * sw + i, py = text_y + r * sh + j;
                if (text_clip) put_clipped(px, py, col); else put_noclip(px, py, col);
            }
        }
    }
    text_x += 8 * sw;
}
void gfx_PrintStringXY(const char *s, int x, int y) {
    gfx_SetTextXY(x, y);
    while (*s) gfx_PrintChar(*s++);
}

int shim_write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    int x, y;
    if (!f) return -1;
    fprintf(f, "P6\n320 240\n255\n");
    for (y = 0; y < 240; y++) for (x = 0; x < 320; x++) {
        uint16_t v = shim_pal[shim_fb[y][x]];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(((v >> 10) & 31) * 255 / 31);
        rgb[1] = (unsigned char)(((v >> 5) & 31) * 255 / 31);
        rgb[2] = (unsigned char)((v & 31) * 255 / 31);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

/* keypad / usb / serial stubs */
uint8_t kb_Data[8];
void kb_Scan(void) {}
bool kb_AnyKey(void) { return true; }
int usb_GetRole(void) { return 0; }
void usb_ResetDevice(usb_device_t d) { (void)d; }
usb_device_t usb_FindDevice(usb_device_t a, usb_device_t b, int f) { (void)a; (void)b; (void)f; return NULL; }
usb_error_t usb_Init(usb_event_callback_t cb, void *d, const usb_standard_descriptors_t *s, int f) { (void)cb; (void)d; (void)s; (void)f; return 0; }
usb_error_t usb_HandleEvents(void) { return 0; }
void usb_Cleanup(void) {}
srl_error_t srl_Open(srl_device_t *s, usb_device_t d, void *b, size_t n, int i, int baud) { (void)s; (void)d; (void)b; (void)n; (void)i; (void)baud; return 0; }
void srl_Close(srl_device_t *s) { (void)s; }
usb_error_t srl_UsbEventCallback(usb_event_t e, void *d, usb_callback_data_t *c) { (void)e; (void)d; (void)c; return 0; }
int srl_Read(srl_device_t *s, void *b, size_t n) { (void)s; (void)b; (void)n; return 0; }
int srl_Write(srl_device_t *s, const void *b, size_t n) { (void)s; (void)b; return (int)n; }
static usb_standard_descriptors_t desc;
const usb_standard_descriptors_t *srl_GetCDCStandardDescriptors(void) { return &desc; }
