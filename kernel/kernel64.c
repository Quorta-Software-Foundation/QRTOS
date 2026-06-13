#include "graphics/graphics.h"
#include "fs/fas32q.h"

typedef struct {
    unsigned int magic;
    unsigned int framebuffer;
    unsigned short width;
    unsigned short height;
    unsigned short pitch;
    unsigned short bpp;
} boot_fb_info_t;

static volatile boot_fb_info_t* boot_fb_info = (volatile boot_fb_info_t*)0x8000;

static unsigned short screen_cells[25][80];
static int ui_cell_w = 10;
static int ui_cell_h = 24;

static int term_x0 = 2;
static int term_y0 = 9;
static int term_w = 54;
static int term_h = 13;
static int term_x = 0;
static int term_y = 0;
static unsigned char term_attr = 0x1F;

static unsigned char shift_pressed = 0;

typedef enum {
    VIEW_HOME = 0,
    VIEW_APPS,
    VIEW_NOTEPAD,
    VIEW_FILES,
    VIEW_SHUTDOWN
} desktop_view_t;

static desktop_view_t current_view = VIEW_HOME;
static int mouse_x = 40;
static int mouse_y = 12;
static unsigned char mouse_left = 0;
static unsigned char mouse_prev_left = 0;
static unsigned char mouse_packet[3];
static int mouse_packet_index = 0;
static const char* mouse_hover_label = "desktop";

static const char scancode_to_ascii[] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};

/* forward declarations used by window manager */
static void vga_put(int x, int y, char ch, unsigned char attr);
static void vga_fill(int x, int y, int w, int h, char ch, unsigned char attr);
static void vga_print(int x, int y, const char* s, unsigned char attr);
static void draw_box(int x, int y, int w, int h, const char* title, unsigned char body_attr, unsigned char border_attr);

static const char scancode_to_ascii_shift[] = {
    0,0,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' '
};

/* Lightweight window manager */
#define MAX_WINDOWS 4
typedef struct {
    int id;
    int x, y, w, h;
    char title[24];
    unsigned char title_attr, body_attr, border_attr;
    int visible;
    int z;
    int dragging;
    int buf_len;
    char* buf;
    int cursor_pos;
} window_t;

static window_t windows[MAX_WINDOWS];
static char win_buffers[MAX_WINDOWS][512];
static int window_count = 0;
static int active_window = -1;
static int z_counter = 1;
static int start_menu_open = 0;

typedef enum {
    WIN_APP_NOTEPAD = 1,
    WIN_APP_FILES = 2,
    WIN_APP_EXPLORER = 3
} window_app_t;

static void draw_window(int idx) {
    window_t* w = &windows[idx];
    if (!w->visible) return;
    
    // Convert cell coordinates to pixel coordinates
    int px = w->x * 10;
    int py = w->y * 24;
    int pw = w->w * 10;
    int ph = w->h * 24;
    int titlebar_h = 24;
    
    // Simple Linux-style window: light gray background
    gfx_rect_fill(px, py, pw, ph, 7);  // Light gray
    
    // Black border
    gfx_rect(px, py, pw, ph, 0);
    
    // Titlebar (dark gray)
    gfx_rect_fill(px + 1, py + 1, pw - 2, titlebar_h - 2, 8);
    gfx_str(px + 10, py + 8, w->title, 15, 8);  // White title
    
    // Close button area
    gfx_str(px + pw - 20, py + 8, "X", 15, 8);
    
    // Draw content area
    int content_y = py + titlebar_h + 2;
    int content_h = ph - titlebar_h - 4;
    
    if (w->buf) {
        int i = 0;
        int cx = 0, cy = 0;
        int sx = px + 8, sy = content_y;
        int line_height = 12;
        
        for (i = 0; i < w->buf_len && sy + cy * line_height < py + ph - 10; i++) {
            char ch = w->buf[i];
            if (ch == '\n') { cx = 0; cy++; continue; }
            gfx_char(sx + cx * 8, sy + cy * line_height, ch, 0, 7);  // Black on gray
            cx++;
            if (cx >= (pw - 16) / 8) { cx = 0; cy++; }
        }
        
        // Draw cursor
        if (active_window == idx) {
            int cpos = w->cursor_pos;
            int ccx = cpos % ((pw - 16) / 8);
            int ccy = cpos / ((pw - 16) / 8);
            if (sy + ccy * line_height < py + ph - 10) {
                gfx_char(sx + ccx * 8, sy + ccy * line_height, '_', 0, 7);
            }
        }
    }
}

static void draw_windows() {
    int i, j;
    /* draw by increasing z */
    for (i = 1; i <= z_counter; i++) {
        for (j = 0; j < MAX_WINDOWS; j++) {
            if (windows[j].visible && windows[j].z == i) draw_window(j);
        }
    }
}

static int create_window(const char* title, int x, int y, int w, int h) {
    int i;
    for (i = 0; i < MAX_WINDOWS; i++) if (!windows[i].visible) break;
    if (i == MAX_WINDOWS) return -1;
    windows[i].id = i;
    windows[i].x = x; windows[i].y = y; windows[i].w = w; windows[i].h = h;
    int tlen = 0; while (title[tlen] && tlen < (int)sizeof(windows[i].title)-1) { windows[i].title[tlen]=title[tlen]; tlen++; }
    windows[i].title[tlen]=0;
    windows[i].title_attr = 0x1E;
    windows[i].body_attr = 0x1F;
    windows[i].border_attr = 0x4F;
    windows[i].visible = 1;
    windows[i].z = ++z_counter;
    windows[i].dragging = 0;
    windows[i].buf = win_buffers[i];
    windows[i].buf_len = 0;
    windows[i].cursor_pos = 0;
    window_count++;
    active_window = i;
    return i;
}

static int create_app_window(window_app_t app, const char* title, int x, int y, int w, int h) {
    int idx = create_window(title, x, y, w, h);
    if (idx < 0) return -1;
    windows[idx].id = app;
    return idx;
}

static void close_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    windows[idx].visible = 0;
    if (active_window == idx) active_window = -1;
}

static void bring_window_to_front(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    windows[idx].z = ++z_counter;
    active_window = idx;
}

static void open_notepad_window() {
    int idx = create_app_window(WIN_APP_NOTEPAD, "Notepad.qbin", 18, 4, 44, 15);
    if (idx < 0) return;
    /* seed with text */
    const char* starter = "Welcome to Notepad.qbin\n\nThis app loads as a .qbin package.\nType to add text. Backspace works too.";
    int i=0; while (starter[i] && i < (int)sizeof(win_buffers[idx])-1) { win_buffers[idx][i]=starter[i]; i++; }
    windows[idx].buf_len = i;
    windows[idx].cursor_pos = windows[idx].buf_len;
}

static void open_files_window() {
    int idx = create_app_window(WIN_APP_FILES, "File Explorer", 12, 4, 56, 15);
    int i;
    if (idx < 0) return;
    windows[idx].buf_len = 0;
    for (i = 0; i < FAS32Q_MAX_FILES; i++) {
        if (ram_files[i].flags == 0) continue;
        if (ram_files[i].flags & FAS32Q_FLAG_DELETED) continue;
        if (windows[idx].buf_len < (int)sizeof(win_buffers[idx]) - 32) {
            char* buf = windows[idx].buf;
            int p = windows[idx].buf_len;
            int j = 0;
            const char* tag = (ram_files[i].flags & FAS32Q_FLAG_FOLDER) ? "[DIR]  " : "[FILE] ";
            while (tag[j]) buf[p++] = tag[j++];
            j = 0;
            while (ram_files[i].name[j]) buf[p++] = ram_files[i].name[j++];
            buf[p++] = '\n';
            windows[idx].buf_len = p;
        }
    }
    if (windows[idx].buf_len == 0) {
        const char* empty = "This folder is empty.\n\nUse touch or mkdir from the shell.";
        i = 0;
        while (empty[i] && i < (int)sizeof(win_buffers[idx]) - 1) {
            windows[idx].buf[i] = empty[i];
            i++;
        }
        windows[idx].buf_len = i;
    }
    windows[idx].cursor_pos = windows[idx].buf_len;
}


static void outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void outw(unsigned short port, unsigned short val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void wait_for_key_ready() {
    while (!(inb(0x64) & 0x01)) {
    }
}

static void vga_put(int x, int y, char ch, unsigned char attr) {
    int px, py, tx, ty;
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return;
    screen_cells[y][x] = ((unsigned short)attr << 8) | (unsigned char)ch;
    px = x * ui_cell_w;
    py = y * ui_cell_h;
    gfx_rect_fill(px, py, ui_cell_w, ui_cell_h, (unsigned char)(attr >> 4));
    tx = px + (ui_cell_w - 8) / 2;
    ty = py + (ui_cell_h - 8) / 2;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    gfx_char(tx, ty, ch, attr & 0x0F, (attr >> 4) & 0x0F);
}

static void vga_fill(int x, int y, int w, int h, char ch, unsigned char attr) {
    int row, col;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            vga_put(x + col, y + row, ch, attr);
        }
    }
}

static void vga_print(int x, int y, const char* s, unsigned char attr) {
    int i;
    for (i = 0; s[i]; i++) {
        vga_put(x + i, y, s[i], attr);
    }
}

static void draw_hline(int x, int y, int w, char ch, unsigned char attr) {
    int i;
    for (i = 0; i < w; i++) vga_put(x + i, y, ch, attr);
}

static void draw_box(int x, int y, int w, int h, const char* title, unsigned char body_attr, unsigned char border_attr) {
    int i;
    draw_hline(x, y, w, '-', border_attr);
    draw_hline(x, y + h - 1, w, '-', border_attr);
    for (i = 0; i < h; i++) {
        vga_put(x, y + i, '|', border_attr);
        vga_put(x + w - 1, y + i, '|', border_attr);
    }
    vga_put(x, y, '+', border_attr);
    vga_put(x + w - 1, y, '+', border_attr);
    vga_put(x, y + h - 1, '+', border_attr);
    vga_put(x + w - 1, y + h - 1, '+', border_attr);
    vga_fill(x + 1, y + 1, w - 2, h - 2, ' ', body_attr);
    if (title) vga_print(x + 2, y, title, border_attr);
}

void term_scroll() {
    int row, col;
    for (row = 0; row < term_h - 1; row++) {
        for (col = 0; col < term_w; col++) {
            screen_cells[term_y0 + row][term_x0 + col] =
                screen_cells[term_y0 + row + 1][term_x0 + col];
            {
                unsigned short cell = screen_cells[term_y0 + row][term_x0 + col];
                vga_put(term_x0 + col, term_y0 + row, (char)(cell & 0xFF), (unsigned char)(cell >> 8));
            }
        }
    }
    for (col = 0; col < term_w; col++) {
        vga_put(term_x0 + col, term_y0 + term_h - 1, ' ', term_attr);
    }
    term_y = term_h - 1;
}

void term_clear() {
    vga_fill(term_x0, term_y0, term_w, term_h, ' ', term_attr);
    term_x = 0;
    term_y = 0;
}

static void term_newline() {
    term_x = 0;
    term_y++;
    if (term_y >= term_h) term_scroll();
}

void term_putc(char c, unsigned char attr) {
    if (c == '\n') { term_newline(); return; }
    if (c == '\b') {
        if (term_x > 0) {
            term_x--;
            vga_put(term_x0 + term_x, term_y0 + term_y, ' ', attr);
        }
        return;
    }
    vga_put(term_x0 + term_x, term_y0 + term_y, c, attr);
    term_x++;
    if (term_x >= term_w) term_newline();
}

void term_puts(const char* s, unsigned char attr) {
    int i;
    for (i = 0; s[i]; i++) term_putc(s[i], attr);
}

void term_putln(const char* s, unsigned char attr) {
    term_puts(s, attr);
    term_putc('\n', attr);
}

void term_prompt() {
    term_puts("C:\\QRTOS> ", 0x1E);
}

static void int_to_str(int n, char* buf) {
    int i = 0;
    int neg = 0;
    char tmp[32];
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    if (n < 0) { neg = 1; n = -n; }
    while (n > 0) {
        tmp[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static int str_to_int(const char* s) {
    int i = 0;
    int neg = 0;
    int result = 0;
    if (s[0] == '-') { neg = 1; i = 1; }
    while (s[i] >= '0' && s[i] <= '9') {
        result = result * 10 + (s[i] - '0');
        i++;
    }
    return neg ? -result : result;
}

static unsigned char bcd_to_bin(unsigned char bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static unsigned char read_cmos(unsigned char reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static void pad_zero(int n, char* buf) {
    if (n < 10) {
        buf[0] = '0';
        buf[1] = (char)('0' + n);
        buf[2] = '\0';
    } else {
        int_to_str(n, buf);
    }
}

static void draw_bar(int percent, unsigned char attr) {
    int i;
    int bars = (percent * 20) / 100;
    term_putc('[', 0x08);
    for (i = 0; i < 20; i++) term_putc(i < bars ? '#' : '.', attr);
    term_putc(']', 0x08);
    term_putc(' ', 0x08);
    char buf[8];
    int_to_str(percent, buf);
    term_puts(buf, attr);
    term_putln("%", 0x08);
}

static int streq(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int startswith(const char* str, const char* prefix) {
    int i = 0;
    while (prefix[i]) {
        if (str[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static void render_static_ui();
static void render_files();
static void draw_app_dock();
static void redraw_view();
static void app_show_banner(const char* name, unsigned char accent);
static void app_show_apps();
static void shell_poweroff();
static void draw_mouse_cursor();

static void mouse_wait_write() {
    while (inb(0x64) & 0x02) {
    }
}

static void mouse_wait_read() {
    while (!(inb(0x64) & 0x01)) {
    }
}

static void mouse_write(unsigned char value) {
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, value);
    mouse_wait_read();
    (void)inb(0x60);
}

static void mouse_init() {
    mouse_wait_write();
    outb(0x64, 0xA8);
    mouse_write(0xF6);
    mouse_write(0xF4);
    mouse_packet_index = 0;
    mouse_left = 0;
    mouse_prev_left = 0;
}

static int rect_hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

static void draw_button(int x, int y, int w, int h, const char* label, int hot) {
    unsigned char body = hot ? 0x36 : 0x24;
    vga_fill(x, y, w, h, ' ', body);
    vga_fill(x, y, w, 1, ' ', hot ? 0x4F : 0x2A);
    vga_print(x + 2, y + 1, label, 0x0F);
}

static void draw_task_button(int x, int y, int w, const char* label, int active) {
    // Convert from cell coordinates to pixel coordinates
    int px = x * 10;
    int py = y * 24;
    int pw = w * 10;
    int ph = 24;
    
    if (active) {
        // Active/pressed button
        gfx_rect_beveled(px, py, pw, ph, WIN95_FACE, WIN95_SHADOW, WIN95_HIGHLIGHT);
    } else {
        // Inactive button
        gfx_rect_beveled(px, py, pw, ph, WIN95_FACE, WIN95_HIGHLIGHT, WIN95_SHADOW);
    }
    
    // Draw label text
    gfx_str(px + 5, py + 8, label, WIN95_TEXT, WIN95_FACE);
}

static void draw_start_menu() {
    // Draw start menu as a beveled window
    int x = 0 * 10;
    int y = 14 * 24;
    int w = 26 * 10;
    int h = 9 * 24;
    
    gfx_rect_beveled(x, y, w, h, WIN95_FACE, WIN95_HIGHLIGHT, WIN95_SHADOW);
    
    // Draw menu title bar
    gfx_rect_fill(x + 2, y + 5, w - 4, 20, WIN95_BG);
    gfx_str(x + 10, y + 8, "Start", WIN95_TEXT, WIN95_BG);
    
    // Draw menu items
    gfx_str(x + 10, y + 45, "Notepad.qbin", WIN95_TEXT, WIN95_FACE);
    gfx_str(x + 10, y + 65, "File Explorer", WIN95_TEXT, WIN95_FACE);
    gfx_str(x + 10, y + 85, "Shell", WIN95_TEXT, WIN95_FACE);
    gfx_str(x + 10, y + 105, "Shutdown", WIN95_TEXT, WIN95_FACE);
}

static int start_button_hit(int x, int y) {
    return rect_hit(x, y, 1, 23, 8, 1);
}

static int taskbar_window_hit(int x, int y) {
    int slot_x = 10;
    int i;
    if (y != 23) return -1;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].visible) continue;
        if (rect_hit(x, y, slot_x, 23, 12, 1)) return i;
        slot_x += 13;
    }
    return -1;
}

static int start_menu_hit(int x, int y) {
    if (!start_menu_open) return 0;
    if (!rect_hit(x, y, 0, 16, 24, 7)) return 0;
    if (y == 17) return 1;
    if (y == 18) return 2;
    if (y == 19) return 3;
    if (y == 20) return 4;
    return 0;
}

static void draw_status_bar() {
    char buf[16];
    vga_fill(0, 24, 80, 1, ' ', 0x1A);
    vga_print(2, 24, "Mouse", 0xF0);
    int_to_str(mouse_x, buf);
    vga_print(9, 24, buf, 0xF0);
    vga_put(11, 24, ',', 0xF0);
    int_to_str(mouse_y, buf);
    vga_print(12, 24, buf, 0xF0);
    vga_print(18, 24, "Hover", 0xF0);
    vga_print(25, 24, mouse_hover_label, 0xF0);
    vga_print(57, 24, "QRTOS", 0xF0);
}

static void draw_app_dock() {
    // Draw minimal status bar at top (like Linux kernel boot)
    int status_h = 20;
    gfx_rect_fill(0, 0, 800, status_h, 1);  // Teal bar
    gfx_str(10, 5, "QRTOS 1.0 | F1=Help | F2=Apps | F3=Files", 15, 1);
    
    // Draw minimal taskbar at bottom
    int taskbar_y = 576;
    int taskbar_h = 24;
    gfx_rect_fill(0, taskbar_y, 800, taskbar_h, 8);  // Dark gray
    
    // Status on right side
    char buf[32];
    gfx_str(600, taskbar_y + 5, "Mouse:", 15, 8);
    int_to_str(mouse_x, buf);
    gfx_str(660, taskbar_y + 5, buf, 15, 8);
    
    if (start_menu_open) {
        // Simple command menu
        gfx_rect_fill(10, 100, 200, 300, 8);
        gfx_str(20, 110, "Apps:", 14, 8);
        gfx_str(20, 130, "notepad", 15, 8);
        gfx_str(20, 150, "files", 15, 8);
        gfx_str(20, 170, "shell", 15, 8);
        gfx_str(20, 190, "help", 15, 8);
    }
}

static void redraw_view();

static void show_home_view() {
    current_view = VIEW_HOME;
    render_static_ui();
    render_files();
    draw_app_dock();
    draw_windows();
}

static void show_apps_view() {
    current_view = VIEW_APPS;
    app_show_banner(" Apps ", 0x1E);
    vga_print(5, 4, "Available applications:", 0x0A);
    vga_print(6, 6, "notepad.qbin - open the editor app", 0x0F);
    vga_print(6, 7, "files    - browse FAS32Q entries", 0x0F);
    vga_print(6, 8, "home     - return to desktop", 0x0F);
    vga_print(6, 9, "shutdown - power off QRTOS", 0x0F);
    vga_print(6, 10, "poweroff - alias for shutdown", 0x0F);
    vga_print(6, 12, "Launcher shortcuts are also shown on the desktop.", 0x08);
    draw_app_dock();
    draw_windows();
}

static void show_notepad_view() {
    static const char* note_lines[] = {
        "QRTOS Notepad.qbin",
        "",
        "This is the editor app entry.",
        "The windowed app launches as a .qbin package.",
        "Use the Start menu or command alias.",
        "",
        "Try: files, ls, mkdir docs, touch todo.txt"
    };
    int i;
    current_view = VIEW_NOTEPAD;
    app_show_banner(" Notepad ", 0x1A);
    for (i = 0; i < 7; i++) {
        vga_print(5, 4 + i, note_lines[i], i == 0 ? 0x1E : 0x0F);
    }
    vga_print(5, 13, "[Endpoint] notepad", 0x0B);
    draw_app_dock();
    draw_windows();
}

static void show_files_view() {
    int i;
    int y = 5;
    current_view = VIEW_FILES;
    app_show_banner(" Files ", 0x1B);
    vga_print(5, 4, "FAS32Q files and folders:", 0x0A);
    for (i = 0; i < FAS32Q_MAX_FILES && y < 18; i++) {
        if (ram_files[i].flags == 0) continue;
        if (ram_files[i].flags & FAS32Q_FLAG_DELETED) continue;
        vga_print(5, y, (ram_files[i].flags & FAS32Q_FLAG_FOLDER) ? "[DIR]  " : "[FILE] ", 0x0B);
        vga_print(12, y, ram_files[i].name, 0x0F);
        y++;
    }
    if (y == 5) vga_print(5, y, "(empty)", 0x08);
    vga_print(5, 19, "[Endpoint] files", 0x0B);
    draw_app_dock();
    draw_windows();
}

static void show_shutdown_view() {
    current_view = VIEW_SHUTDOWN;
    app_show_banner(" Shutdown ", 0x4F);
    vga_print(5, 5, "Use the shutdown command to power off QRTOS.", 0x0F);
    vga_print(5, 6, "Use the reboot command to restart.", 0x0F);
    vga_print(5, 8, "Kernel endpoint: shutdown()", 0x0C);
    vga_print(5, 9, "Kernel endpoint: poweroff()", 0x0C);
    draw_app_dock();
    draw_windows();
}

static void redraw_view() {
    if (current_view == VIEW_HOME) show_home_view();
    else if (current_view == VIEW_APPS) show_apps_view();
    else if (current_view == VIEW_NOTEPAD) show_notepad_view();
    else if (current_view == VIEW_FILES) show_files_view();
    else show_shutdown_view();
    /* draw any windows on top of the current view */
    draw_windows();
}

static int button_at(int x, int y) {
    if (rect_hit(x, y, 2, 0, 8, 2)) return 1;
    if (rect_hit(x, y, 11, 0, 8, 2)) return 2;
    if (rect_hit(x, y, 20, 0, 11, 2)) return 3;
    if (rect_hit(x, y, 32, 0, 9, 2)) return 4;
    if (rect_hit(x, y, 42, 0, 11, 2)) return 5;
    return 0;
}

static void handle_button_click(int button) {
    if (button == 1) { show_home_view(); return; }
    if (button == 2) { show_apps_view(); return; }
    if (button == 3) { open_notepad_window(); return; }
    if (button == 4) { show_files_view(); return; }
    if (button == 5) { show_shutdown_view(); shell_poweroff(); return; }
}

static void mouse_update_label() {
    int menu = start_menu_hit(mouse_x, mouse_y);
    if (start_button_hit(mouse_x, mouse_y)) { mouse_hover_label = "start"; return; }
    if (menu == 1) { mouse_hover_label = "notepad"; return; }
    if (menu == 2) { mouse_hover_label = "files"; return; }
    if (menu == 3) { mouse_hover_label = "shell"; return; }
    if (menu == 4) { mouse_hover_label = "shutdown"; return; }
    if (taskbar_window_hit(mouse_x, mouse_y) >= 0) { mouse_hover_label = "taskbar"; return; }
    int hot = button_at(mouse_x, mouse_y);
    if (hot == 1) mouse_hover_label = "home";
    else if (hot == 2) mouse_hover_label = "apps";
    else if (hot == 3) mouse_hover_label = "notepad";
    else if (hot == 4) mouse_hover_label = "files";
    else if (hot == 5) mouse_hover_label = "shutdown";
    else mouse_hover_label = "desktop";
}

static void mouse_apply_packet() {
    signed char dx = (signed char)mouse_packet[1];
    signed char dy = (signed char)mouse_packet[2];
    int old_x = mouse_x;
    int old_y = mouse_y;

    if (mouse_packet[0] & 0x40) dx = 0;
    if (mouse_packet[0] & 0x80) dy = 0;

    /* use full delta */
    mouse_x += dx;
    mouse_y -= dy;

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > 79) mouse_x = 79;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > 24) mouse_y = 24;

    mouse_left = (mouse_packet[0] & 0x01) ? 1 : 0;

    /* find topmost window under cursor */
    int top_idx = -1; int top_z = -1; int i;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].visible) continue;
        if (rect_hit(mouse_x, mouse_y, windows[i].x, windows[i].y, windows[i].w, windows[i].h)) {
            if (windows[i].z > top_z) { top_z = windows[i].z; top_idx = i; }
        }
    }

    if (mouse_left && !mouse_prev_left) {
        int menu = start_menu_hit(mouse_x, mouse_y);
        int task = taskbar_window_hit(mouse_x, mouse_y);
        if (start_button_hit(mouse_x, mouse_y)) {
            start_menu_open = !start_menu_open;
            draw_app_dock();
            mouse_prev_left = mouse_left;
            return;
        }
        if (menu == 1) { open_notepad_window(); start_menu_open = 0; redraw_view(); mouse_prev_left = mouse_left; return; }
        if (menu == 2) { open_files_window(); start_menu_open = 0; redraw_view(); mouse_prev_left = mouse_left; return; }
        if (menu == 3) { start_menu_open = 0; app_show_apps(); mouse_prev_left = mouse_left; return; }
        if (menu == 4) { start_menu_open = 0; show_shutdown_view(); shell_poweroff(); mouse_prev_left = mouse_left; return; }
        if (task >= 0) { bring_window_to_front(task); start_menu_open = 0; redraw_view(); mouse_prev_left = mouse_left; return; }
        /* press: check close button or titlebar or content */
        if (top_idx >= 0) {
            window_t* w = &windows[top_idx];
            /* close button area */
            if (rect_hit(mouse_x, mouse_y, w->x + w->w - 3, w->y + 1, 1, 1)) {
                close_window(top_idx);
                start_menu_open = 0;
                redraw_view();
                mouse_prev_left = mouse_left;
                return;
            }
            /* titlebar area (y+1) */
            if (rect_hit(mouse_x, mouse_y, w->x + 1, w->y + 1, w->w - 2, 1)) {
                bring_window_to_front(top_idx);
                w->dragging = 1;
                start_menu_open = 0;
                mouse_prev_left = mouse_left;
                return;
            }
            /* content click -> focus */
            bring_window_to_front(top_idx);
            start_menu_open = 0;
            redraw_view();
            mouse_prev_left = mouse_left;
            return;
        }
        /* otherwise check dock buttons */
        handle_button_click(button_at(mouse_x, mouse_y));
    }

    /* dragging move */
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].visible && windows[i].dragging) {
            windows[i].x += dx;
            windows[i].y -= dy;
            if (windows[i].x < 0) windows[i].x = 0;
            if (windows[i].y < 0) windows[i].y = 0;
            if (windows[i].x + windows[i].w > 80) windows[i].x = 80 - windows[i].w;
            if (windows[i].y + windows[i].h > 24) windows[i].y = 24 - windows[i].h;
            redraw_view();
        }
    }

    if (!mouse_left && mouse_prev_left) {
        /* release -> stop dragging */
        for (i = 0; i < MAX_WINDOWS; i++) windows[i].dragging = 0;
    }

    mouse_prev_left = mouse_left;
    if (old_x != mouse_x || old_y != mouse_y) {
        mouse_update_label();
        draw_app_dock();
    }
}

static void draw_mouse_cursor() {
    vga_put(mouse_x, mouse_y, '@', 0x4F);
}

static void mouse_poll() {
    while ((inb(0x64) & 0x21) == 0x21) {
        unsigned char data = inb(0x60);
        if (mouse_packet_index == 0 && !(data & 0x08)) continue;
        mouse_packet[mouse_packet_index++] = data;
        if (mouse_packet_index == 3) {
            mouse_packet_index = 0;
            mouse_apply_packet();
        }
    }
}

static int keyboard_poll(char* c) {
    unsigned char sc;
    if (!(inb(0x64) & 0x01)) return 0;
    if (inb(0x64) & 0x20) return 0;
    sc = inb(0x60);
    if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return 0; }
    if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; return 0; }
    if (sc & 0x80) return 0;
    *c = shift_pressed ? scancode_to_ascii_shift[sc] : scancode_to_ascii[sc];
    return *c != 0;
}

static void app_show_banner(const char* name, unsigned char accent) {
    vga_fill(2, 2, 76, 20, ' ', 0x1F);
    draw_box(2, 2, 76, 20, name, 0x1F, accent);
}

static void app_show_apps() {
    show_apps_view();
}

static void app_show_notepad() {
    open_notepad_window();
}

static void app_show_files() {
    open_files_window();
}

static void app_show_shutdown_hint() {
    show_shutdown_view();
}

static void app_show_desktop() {
    show_home_view();
}

static void shell_poweroff() {
    term_putln("Powering off QRTOS...", 0x0C);
    app_show_shutdown_hint();
    outw(0x604, 0x2000);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void fs_seed_demo() {
    if (ram_file_count > 0) return;
    fas32q_ram_create("boot.txt", FAS32Q_FLAG_FILE);
    fas32q_ram_create("readme.md", FAS32Q_FLAG_FILE);
    fas32q_ram_create("shell.qs", FAS32Q_FLAG_FILE);
    fas32q_ram_create("config", FAS32Q_FLAG_FOLDER);
    fas32q_ram_create("games", FAS32Q_FLAG_FOLDER);
}

static void render_static_ui() {
    // Linux-style boot screen: solid teal background with white text
    // Clear to teal/cyan (like Linux kernel boot)
    gfx_clear(1);  // Blue/teal background
    
    // Large title at top
    gfx_str(50, 40, "QRTOS x86_64 Kernel", 15, 1);
    
    // System info
    gfx_str(50, 80, "Boot: Custom 2-stage bootloader", 15, 1);
    gfx_str(50, 100, "Mode: Long mode 64-bit", 15, 1);
    gfx_str(50, 120, "Video: 800x600 framebuffer", 15, 1);
    gfx_str(50, 140, "Memory: FAS32Q filesystem", 15, 1);
    
    // Welcome message
    gfx_str(50, 180, "Welcome to QRTOS", 14, 1);
    gfx_str(50, 210, "Type 'help' for commands or click buttons", 15, 1);
    
    // Status
    gfx_str(50, 300, "Ready.", 10, 1);
}

static void render_files() {
    int i;
    int y = 5;
    for (i = 0; i < FAS32Q_MAX_FILES && y < 22; i++) {
        if (ram_files[i].flags == 0) continue;
        if (ram_files[i].flags & FAS32Q_FLAG_DELETED) continue;
        vga_put(3, y, (ram_files[i].flags & FAS32Q_FLAG_FOLDER) ? '[' : '-', 0x0B);
        vga_print(4, y, ram_files[i].name, 0x0F);
        if (ram_files[i].flags & FAS32Q_FLAG_FOLDER) vga_put(4 + 8, y, ']', 0x0B);
        y++;
    }
    if (y == 5) vga_print(3, y++, "(empty)", 0x08);
}
static void shell_help() {
    term_putln("QRTOS Commands:", 0x0A);
    term_putln("  home/desktop - go to desktop", 0x0F);
    term_putln("  apps        - app launcher", 0x0F);
    term_putln("  notepad / notepad.qbin - open notepad app", 0x0F);
    term_putln("  files       - open files app", 0x0F);
    term_putln("  ram         - memory and FAS32Q status", 0x0F);
    term_putln("  cpu         - CPU info", 0x0F);
    term_putln("  clock       - date and time", 0x0F);
    term_putln("  taskmgr     - task manager", 0x0F);
    term_putln("  version     - QRTOS version", 0x0F);
    term_putln("  qsf         - about QSF", 0x0F);
    term_putln("  ls          - list files", 0x0F);
    term_putln("  mkdir name  - create folder", 0x0F);
    term_putln("  touch name  - create file", 0x0F);
    term_putln("  del name    - delete file", 0x0F);
    term_putln("  recycle     - show deleted", 0x0F);
    term_putln("  restore n   - restore file", 0x0F);
    term_putln("  echo text   - print text", 0x0F);
    term_putln("  calc N op N - calculator", 0x0F);
    term_putln("  color       - color palette", 0x0F);
    term_putln("  reboot      - reboot", 0x0F);
    term_putln("  shutdown    - power off", 0x0F);
    term_putln("  poweroff    - power off", 0x0F);
}

static void shell_sysinfo() {
    term_putln("System Information:", 0x0A);
    term_putln("  OS:         QRTOS v1", 0x0F);
    term_putln("  Arch:       x86_64 long mode", 0x0F);
    term_putln("  Console:    80x25 VGA text mode", 0x0F);
    term_putln("  Filesystem: FAS32Q v1", 0x0F);
    term_putln("  GUI:        text desktop / shell", 0x0F);
}

static void shell_version() {
    term_putln("QRTOS v1 | 64-bit kernel | FAS32Q", 0x0A);
}

static void shell_qsf() {
    term_putln("Qourtra Software Foundation", 0x0A);
    term_putln("Building QRTOS from scratch", 0x0F);
    term_putln("FAS32Q custom filesystem", 0x0F);
    term_putln("Est. 2026", 0x0F);
}

static void shell_ram() {
    int used = 10;
    int total = 640;
    int free = total - used;
    term_putln("Memory / FAS32Q status:", 0x0A);
    term_puts("  Total: ", 0x0F); int_to_str(total, (char[16]){0});
    {
        char buf[16];
        int_to_str(total, buf); term_putln(buf, 0x0F);
    }
    term_puts("  Used:  ", 0x0F); { char buf[16]; int_to_str(used, buf); term_putln(buf, 0x0C); }
    term_puts("  Free:  ", 0x0F); { char buf[16]; int_to_str(free, buf); term_putln(buf, 0x0A); }
    term_putln("  FAS32Q files:", 0x0F);
    {
        int i;
        for (i = 0; i < FAS32Q_MAX_FILES; i++) {
            if (ram_files[i].flags == 0) continue;
            if (ram_files[i].flags & FAS32Q_FLAG_DELETED) continue;
            term_puts("    ", 0x08);
            term_puts(ram_files[i].name, 0x0F);
            if (ram_files[i].flags & FAS32Q_FLAG_FOLDER) term_puts(" [DIR]", 0x0B);
            term_putc('\n', 0x0F);
        }
    }
}

static void shell_taskmgr() {
    term_putln("Task Manager:", 0x0A);
    term_putln("  kernel   Running  CPU 2%", 0x0F);
    term_putln("  shell    Running  CPU 1%", 0x0F);
    term_putln("  fs       Running  CPU 1%", 0x0F);
    term_putln("  idle     Running  CPU 95%", 0x0F);
}

static void shell_cpu() {
    unsigned int eax, ebx, ecx, edx;
    char vendor[13];
    __asm__ volatile("cpuid"
        : "=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx) : "a"(0));
    vendor[0] = (ebx >> 0) & 0xFF; vendor[1] = (ebx >> 8) & 0xFF;
    vendor[2] = (ebx >> 16) & 0xFF; vendor[3] = (ebx >> 24) & 0xFF;
    vendor[4] = (edx >> 0) & 0xFF; vendor[5] = (edx >> 8) & 0xFF;
    vendor[6] = (edx >> 16) & 0xFF; vendor[7] = (edx >> 24) & 0xFF;
    vendor[8] = (ecx >> 0) & 0xFF; vendor[9] = (ecx >> 8) & 0xFF;
    vendor[10] = (ecx >> 16) & 0xFF; vendor[11] = (ecx >> 24) & 0xFF;
    vendor[12] = '\0';
    term_putln("CPU Info:", 0x0A);
    term_puts("  Vendor: ", 0x0F); term_putln(vendor, 0x0F);
    __asm__ volatile("cpuid"
        : "=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx) : "a"(1));
    term_putln("  Mode: x86_64 long mode", 0x0F);
    term_puts("  Features: ", 0x0F);
    if (edx & (1 << 25)) term_puts("SSE ", 0x0B);
    if (edx & (1 << 26)) term_puts("SSE2 ", 0x0B);
    if (ecx & (1 << 0)) term_puts("SSE3 ", 0x0B);
    if (edx & (1 << 4)) term_puts("TSC ", 0x0B);
    if (edx & (1 << 23)) term_puts("MMX ", 0x0B);
    term_putc('\n', 0x0F);
}

static void shell_clock() {
    unsigned char sec = bcd_to_bin(read_cmos(0x00));
    unsigned char min = bcd_to_bin(read_cmos(0x02));
    unsigned char hour = bcd_to_bin(read_cmos(0x04));
    unsigned char day = bcd_to_bin(read_cmos(0x07));
    unsigned char mon = bcd_to_bin(read_cmos(0x08));
    unsigned char year = bcd_to_bin(read_cmos(0x09));
    char sh[4], sm[4], ss[4], sd[4], smo[4], sy[8];
    pad_zero(hour, sh); pad_zero(min, sm); pad_zero(sec, ss);
    pad_zero(day, sd); pad_zero(mon, smo); int_to_str(2000 + year, sy);
    term_putln("Clock:", 0x0A);
    term_puts("  Time: ", 0x0F); term_puts(sh, 0x0F); term_puts(":", 0x08); term_puts(sm, 0x0F); term_puts(":", 0x08); term_putln(ss, 0x0F);
    term_puts("  Date: ", 0x0F); term_puts(sd, 0x0F); term_puts("/", 0x08); term_puts(smo, 0x0F); term_puts("/", 0x08); term_putln(sy, 0x0F);
}

static void shell_color() {
    int i;
    term_putln("Color palette:", 0x0A);
    for (i = 0; i < 16; i++) {
        char buf[8];
        term_puts("  Color ", (unsigned char)i);
        int_to_str(i, buf);
        term_puts(buf, (unsigned char)i);
        term_putc('\n', (unsigned char)i);
    }
}

static void shell_ls() {
    int i;
    term_putln("Files:", 0x0A);
    for (i = 0; i < FAS32Q_MAX_FILES; i++) {
        if (ram_files[i].flags == 0) continue;
        if (ram_files[i].flags & FAS32Q_FLAG_DELETED) continue;
        term_puts("  ", 0x08);
        term_puts((ram_files[i].flags & FAS32Q_FLAG_FOLDER) ? "[DIR]  " : "[FILE] ", 0x0B);
        term_putln(ram_files[i].name, 0x0F);
    }
}

static void shell_mkdir(const char* name) {
    if (fas32q_ram_create(name, FAS32Q_FLAG_FOLDER) == -1) term_putln("Error: could not create folder", 0x0C);
    else { term_puts("Created folder: ", 0x0A); term_putln(name, 0x0F); }
}

static void shell_touch(const char* name) {
    if (fas32q_ram_create(name, FAS32Q_FLAG_FILE) == -1) term_putln("Error: could not create file", 0x0C);
    else { term_puts("Created file: ", 0x0A); term_putln(name, 0x0F); }
}

static void shell_del(const char* name) {
    if (fas32q_ram_delete(name) == -1) term_putln("Error: file not found", 0x0C);
    else { term_puts("Deleted: ", 0x0C); term_putln(name, 0x0F); }
}

static void shell_recycle() {
    int i;
    term_putln("Recycle bin:", 0x0C);
    for (i = 0; i < FAS32Q_MAX_FILES; i++) {
        if (ram_files[i].flags & FAS32Q_FLAG_DELETED) {
            term_puts("  ", 0x08); term_putln(ram_files[i].name, 0x0C);
        }
    }
}

static void shell_restore(const char* name) {
    if (fas32q_ram_restore(name) == -1) term_putln("Error: not found in recycle bin", 0x0C);
    else { term_puts("Restored: ", 0x0A); term_putln(name, 0x0F); }
}

static void shell_calc(const char* expr) {
    int i = 0;
    char op = 0;
    char num1[32], num2[32], sres[32], sa[32], sb[32];
    int n1 = 0, n2 = 0;
    while (expr[i] == ' ') i++;
    if (expr[i] == '-') num1[n1++] = expr[i++];
    while (expr[i] >= '0' && expr[i] <= '9') num1[n1++] = expr[i++];
    num1[n1] = '\0';
    while (expr[i] == ' ') i++;
    if (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/') op = expr[i++];
    else { term_putln("Error: invalid expression", 0x0C); return; }
    while (expr[i] == ' ') i++;
    if (expr[i] == '-') num2[n2++] = expr[i++];
    while (expr[i] >= '0' && expr[i] <= '9') num2[n2++] = expr[i++];
    num2[n2] = '\0';
    if (n1 == 0 || n2 == 0) { term_putln("Error: missing number", 0x0C); return; }
    {
        int a = str_to_int(num1);
        int b = str_to_int(num2);
        int result = 0;
        if (op == '+') result = a + b;
        else if (op == '-') result = a - b;
        else if (op == '*') result = a * b;
        else if (op == '/') { if (b == 0) { term_putln("Error: division by zero", 0x0C); return; } result = a / b; }
        int_to_str(a, sa); int_to_str(b, sb); int_to_str(result, sres);
        term_puts(sa, 0x0B); term_putc(' ', 0x0F); term_putc(op, 0x0B); term_putc(' ', 0x0F); term_puts(sb, 0x0B); term_puts(" = ", 0x0F); term_putln(sres, 0x0A);
    }
}

static void shell_reboot() {
    term_putln("Rebooting...", 0x0C);
    while (inb(0x64) & 0x02) {
    }
    outb(0x64, 0xFE);
    for (;;) { }
}

static void run_command(const char* cmd) {
    if (streq(cmd, "home") || streq(cmd, "desktop")) { show_home_view(); return; }
    if (streq(cmd, "apps")) { app_show_apps(); return; }
    if (streq(cmd, "notepad")) { app_show_notepad(); redraw_view(); return; }
    if (streq(cmd, "notepad.qbin")) { app_show_notepad(); redraw_view(); return; }
    if (streq(cmd, "files")) { app_show_files(); redraw_view(); return; }
    if (streq(cmd, "help")) { shell_help(); return; }
    if (streq(cmd, "clear")) { term_clear(); return; }
    if (streq(cmd, "sysinfo")) { shell_sysinfo(); return; }
    if (streq(cmd, "ram")) { shell_ram(); return; }
    if (streq(cmd, "cpu")) { shell_cpu(); return; }
    if (streq(cmd, "clock")) { shell_clock(); return; }
    if (streq(cmd, "taskmgr")) { shell_taskmgr(); return; }
    if (streq(cmd, "version")) { shell_version(); return; }
    if (streq(cmd, "qsf")) { shell_qsf(); return; }
    if (streq(cmd, "color")) { shell_color(); return; }
    if (streq(cmd, "ls")) { shell_ls(); return; }
    if (startswith(cmd, "mkdir ")) { shell_mkdir(cmd + 6); return; }
    if (startswith(cmd, "touch ")) { shell_touch(cmd + 6); return; }
    if (startswith(cmd, "del ")) { shell_del(cmd + 4); return; }
    if (streq(cmd, "recycle")) { shell_recycle(); return; }
    if (startswith(cmd, "restore ")) { shell_restore(cmd + 8); return; }
    if (startswith(cmd, "echo ")) { term_putln(cmd + 5, 0x0F); return; }
    if (startswith(cmd, "calc ")) { shell_calc(cmd + 5); return; }
    if (streq(cmd, "shutdown")) { shell_poweroff(); return; }
    if (streq(cmd, "poweroff")) { shell_poweroff(); return; }
    term_puts("Unknown command: ", 0x0C); term_putln(cmd, 0x0F);
    term_putln("Type 'help' for commands.", 0x08);
}

static char read_key() {
    unsigned char sc;
    for (;;) {
        wait_for_key_ready();
        sc = inb(0x60);
        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
        if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; continue; }
        if (sc & 0x80) continue;
        if (shift_pressed) return scancode_to_ascii_shift[sc];
        return scancode_to_ascii[sc];
    }
}

static void desktop_event_loop() {
    char input_buf[128];
    int input_len = 0;
    char c;
    term_prompt();
    for (;;) {
        mouse_poll();
        if (keyboard_poll(&c)) {
            /* if a window is active, route keys to it */
            if (active_window >= 0 && windows[active_window].visible) {
                window_t* w = &windows[active_window];
                if (c == '\b') {
                    if (w->cursor_pos > 0) {
                        int p = --w->cursor_pos;
                        if (p < w->buf_len) {
                            int i; for (i = p; i < w->buf_len; i++) w->buf[i] = w->buf[i+1];
                            w->buf_len--;
                        }
                    }
                } else if (c == '\n') {
                    if (w->buf_len < (int)sizeof(win_buffers[0]) - 1) {
                        w->buf[w->buf_len++] = '\n'; w->cursor_pos = w->buf_len;
                    }
                } else if (c >= 32) {
                    if (w->buf_len < (int)sizeof(win_buffers[0]) - 1) {
                        /* insert at cursor */
                        int i;
                        for (i = w->buf_len; i > w->cursor_pos; i--) w->buf[i] = w->buf[i-1];
                        w->buf[w->cursor_pos++] = c;
                        w->buf_len++;
                    }
                }
                redraw_view();
                continue;
            }
            /* otherwise handle shell input */
            if (c == '\b') {
                if (input_len > 0) {
                    input_len--;
                    term_putc('\b', 0x1E);
                }
                continue;
            }
            if (c == '\n') {
                input_buf[input_len] = '\0';
                term_putc('\n', 0x1E);
                if (input_len > 0) run_command(input_buf);
                input_len = 0;
                term_prompt();
                continue;
            }
            if (input_len < (int)sizeof(input_buf) - 1 && c >= 32) {
                input_buf[input_len++] = c;
                term_putc(c, 0x1E);
            }
        }
    }
}

void kernel_main() {
    if (boot_fb_info->magic == 0x31424651U && boot_fb_info->framebuffer != 0) {
        gfx_init((unsigned char*)(unsigned long)boot_fb_info->framebuffer,
                 boot_fb_info->width,
                 boot_fb_info->height,
                 boot_fb_info->pitch,
                 boot_fb_info->bpp);
    }
    gfx_clear(0);
    if (boot_fb_info->width >= 80) ui_cell_w = (int)boot_fb_info->width / 80;
    if (boot_fb_info->height >= 25) ui_cell_h = (int)boot_fb_info->height / 25;
    if (ui_cell_w < 8) ui_cell_w = 8;
    if (ui_cell_h < 8) ui_cell_h = 8;
    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            screen_cells[y][x] = ((unsigned short)0x00 << 8) | ' ';
        }
    }
    fs_seed_demo();
    mouse_init();
    app_show_desktop();
    term_clear();
    term_putln("QRTOS x86_64 live desktop", 0x0A);
    term_putln("Type apps, notepad, files, or help.", 0x0F);
    desktop_event_loop();
}

