/* Host shim of the graphx API used by main.c (test/render_screens.c only). */
#ifndef SHIM_GRAPHX_H
#define SHIM_GRAPHX_H
#include <stdint.h>
#include <stdbool.h>

typedef uint32_t uint24_t;

#define gfx_RGBTo1555(r, g, b) ((uint16_t)(((((r) & 255) >> 3) << 10) | ((((g) & 255) >> 3) << 5) | (((b) & 255) >> 3)))

enum { gfx_text_clip = 0, gfx_text_noclip = 1 };

void gfx_Begin(void);
void gfx_End(void);
void gfx_SetPalette(const void *palette, uint24_t size, uint24_t offset);
void gfx_SetMonospaceFont(uint8_t spacing);
void gfx_SetTextBGColor(uint8_t c);
void gfx_SetTextTransparentColor(uint8_t c);
void gfx_SetTextConfig(uint8_t config);
void gfx_SetDrawBuffer(void);
void gfx_SwapDraw(void);
void gfx_FillScreen(uint8_t c);
void gfx_SetColor(uint8_t c);
void gfx_FillRectangle(int x, int y, int w, int h);
void gfx_FillRectangle_NoClip(uint24_t x, uint8_t y, uint24_t w, uint8_t h);
void gfx_HorizLine_NoClip(uint24_t x, uint8_t y, uint24_t len);
void gfx_SetTextFGColor(uint8_t c);
void gfx_PrintStringXY(const char *s, int x, int y);
void gfx_SetTextXY(int x, int y);
void gfx_PrintChar(char c);
void gfx_SetTextScale(uint8_t w, uint8_t h);
void gfx_SetClipRegion(int xmin, int ymin, int xmax, int ymax);

/* shim extras */
extern uint8_t  shim_fb[240][320];
extern uint16_t shim_pal[256];
extern int      shim_oob;          /* count of out-of-bounds NoClip writes (would corrupt RAM on the CE) */
int  shim_write_ppm(const char *path);
void shim_load_font(const char *path);
#endif
