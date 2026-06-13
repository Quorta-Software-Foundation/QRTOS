#ifndef GRAPHICS_H
#define GRAPHICS_H

// Default framebuffer-friendly layout values
#define VGA_WIDTH   320
#define VGA_HEIGHT  200
#define TASKBAR_H   14

// Font size (8x8)
#define FONT_W  8
#define FONT_H  8

// Color indices (mode 13h default palette)
#define COL_BLACK    0
#define COL_BLUE     1
#define COL_GREEN    2
#define COL_CYAN     3
#define COL_RED      4
#define COL_MAGENTA  5
#define COL_BROWN    6
#define COL_GREY     7
#define COL_DGREY    8
#define COL_LBLUE    9
#define COL_LGREEN   10
#define COL_LCYAN    11
#define COL_LRED     12
#define COL_LMAGENTA 13
#define COL_YELLOW   14
#define COL_WHITE    15

// Windows 95/98 style colors
#define WIN95_FACE      7    // Light gray button face
#define WIN95_HIGHLIGHT 15   // White (light edge)
#define WIN95_SHADOW    8    // Dark gray (dark edge)
#define WIN95_BG        7    // Light gray background
#define WIN95_TEXT      0    // Black text
#define WIN95_WHITE     15   // White

// Core graphics
void gfx_init(unsigned char* framebuffer, int width, int height, int pitch, int bpp);
void gfx_clear(unsigned char color);
void gfx_pixel(int x, int y, unsigned char color);
void gfx_rect(int x, int y, int w, int h, unsigned char color);
void gfx_rect_fill(int x, int y, int w, int h, unsigned char color);
void gfx_line(int x0, int y0, int x1, int y1, unsigned char color);
void gfx_rect_beveled(int x, int y, int w, int h, unsigned char face, unsigned char highlight, unsigned char shadow);
void gfx_char(int x, int y, char c, unsigned char fg, unsigned char bg);
void gfx_str(int x, int y, const char* s, unsigned char fg, unsigned char bg);

extern unsigned char* fb;
extern int fb_width;
extern int fb_height;
extern int fb_pitch;
extern int fb_bpp;

// UI
void ui_draw_desktop();
void ui_draw_taskbar();
void ui_draw_window(int x, int y, int w, int h, const char* title);
void ui_draw_boot_screen();

#endif
