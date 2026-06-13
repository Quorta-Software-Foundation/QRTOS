#include "fs/fas32q.h"
#include "graphics/graphics.h"

void run_command(char* cmd);

// ── IDT structures ─────────────────────────
struct idt_entry {
    unsigned short base_low;
    unsigned short selector;
    unsigned char zero;
    unsigned char flags;
    unsigned short base_high;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

extern void idt_flush(unsigned int);
extern void irq1_wrapper();

// ── Globals ────────────────────────────────
volatile unsigned char key_buffer[256];
volatile int key_head = 0;
volatile int key_tail = 0;

char input_buf[256];
int input_len = 0;
int shift_pressed = 0;

// ── Port I/O ───────────────────────────────
unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

// ── IDT ────────────────────────────────────
void idt_set_gate(unsigned char num, unsigned int base,
                  unsigned short sel, unsigned char flags) {
    idt[num].base_low  = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector  = sel;
    idt[num].zero      = 0;
    idt[num].flags     = flags;
}

void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base  = (unsigned int)&idt;
    int i;
    for (i = 0; i < 256; i++)
        idt_set_gate(i, 0, 0x08, 0x8E);
    idt_set_gate(0x21, (unsigned int)irq1_wrapper, 0x08, 0x8E);
    idt_flush((unsigned int)&idtp);
}

// ── Keyboard ───────────────────────────────
void irq1_handler() {
    unsigned char sc = inb(0x60);
    outb(0x20, 0x20);
    key_buffer[key_head] = sc;
    key_head = (key_head + 1) % 256;
}

static const char scancode_to_ascii[] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' '
};

static const char scancode_to_ascii_shift[] = {
    0,0,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' '
};

void keyboard_init() {
    idt_install();
    while (inb(0x64) & 0x01) inb(0x60);
    __asm__ volatile("sti");
}

char read_key() {
    while (key_head == key_tail);
    unsigned char sc = key_buffer[key_tail];
    key_tail = (key_tail + 1) % 256;
    if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return 0; }
    if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; return 0; }
    if (sc & 0x80) return 0;
    if (sc < 58 && scancode_to_ascii[sc] != 0) {
        if (shift_pressed) return scancode_to_ascii_shift[sc];
        return scancode_to_ascii[sc];
    }
    return 0;
}

// ── String utils ───────────────────────────
int str_to_int(const char* s) {
    int result = 0; int i = 0; int neg = 0;
    if (s[0] == '-') { neg = 1; i = 1; }
    for (; s[i] >= '0' && s[i] <= '9'; i++)
        result = result * 10 + (s[i] - '0');
    return neg ? -result : result;
}

void int_to_str(int n, char* buf) {
    int i = 0; int neg = 0;
    if (n == 0) { buf[0]='0'; buf[1]='\0'; return; }
    if (n < 0)  { neg=1; n=-n; }
    char tmp[32];
    while (n > 0) { tmp[i++]='0'+(n%10); n/=10; }
    if (neg) tmp[i++]='-';
    int j = 0;
    while (i > 0) buf[j++]=tmp[--i];
    buf[j]='\0';
}

void pad_zero(int n, char* buf) {
    if (n < 10) { buf[0]='0'; buf[1]='0'+n; buf[2]='\0'; }
    else int_to_str(n, buf);
}

unsigned char read_cmos(unsigned char reg) {
    outb(0x70, reg);
    return inb(0x71);
}

unsigned char bcd_to_bin(unsigned char bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// ── Process table ──────────────────────────
#define MAX_PROC 7
char* proc_names[MAX_PROC] = {
    "kernel","shell","vga_driver",
    "kb_driver","fas32q_fs","clock_svc","idle"
};
int proc_state[MAX_PROC] = {1,1,1,1,1,1,1};
int proc_cpu[MAX_PROC]   = {2,1,1,0,1,0,95};
int proc_mem[MAX_PROC]   = {8,2,1,1,2,1,1};

// ── Shell commands ─────────────────────────
void shell_help() {
    term_putln("  QRTOS Commands:", COL_LGREEN);
    term_putln("  help  help2  clear  sysinfo  ram", COL_WHITE);
    term_putln("  cpu   clock  taskmgr  version  qsf", COL_WHITE);
    term_putln("  color  reboot  calc N op N  echo <x>", COL_WHITE);
}

void shell_help2() {
    term_putln("  FAS32Q Commands:", COL_LGREEN);
    term_putln("  ls  mkdir <n>  touch <n>", COL_WHITE);
    term_putln("  del <n>  recycle  restore <n>", COL_WHITE);
}

void shell_sysinfo() {
    term_putln("  QRTOS System Info:", COL_LGREEN);
    term_putln("  OS:    QRTOS v1", COL_WHITE);
    term_putln("  Arch:  x86 32-bit protected mode", COL_WHITE);
    term_putln("  GFX:   VGA 320x200 256 colors", COL_WHITE);
    term_putln("  FS:    FAS32Q v1", COL_WHITE);
    term_putln("  Dev:   Qourtra Software Foundation", COL_WHITE);
}

void shell_version() {
    term_putln("  QRTOS v1 | Bootloader v1 | Kernel v1", COL_LGREEN);
    term_putln("  FAS32Q v1 | VGA Graphics mode", COL_WHITE);
}

void shell_qsf() {
    term_putln("  Qourtra Software Foundation", COL_LGREEN);
    term_putln("  Building QRTOS from scratch", COL_WHITE);
    term_putln("  Est. 2026 | Kolkata, India", COL_WHITE);
}

void shell_reboot() {
    term_putln("  Rebooting...", COL_LRED);
    unsigned char good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    while(1) {}
}

void shell_clock() {
    unsigned char sec  = bcd_to_bin(read_cmos(0x00));
    unsigned char min  = bcd_to_bin(read_cmos(0x02));
    unsigned char hour = bcd_to_bin(read_cmos(0x04));
    unsigned char day  = bcd_to_bin(read_cmos(0x07));
    unsigned char mon  = bcd_to_bin(read_cmos(0x08));
    unsigned char year = bcd_to_bin(read_cmos(0x09));
    char sh[4]; char sm[4]; char ss[4];
    char sd[4]; char smo[4]; char sy[8];
    pad_zero(hour,sh); pad_zero(min,sm); pad_zero(sec,ss);
    pad_zero(day,sd); pad_zero(mon,smo);
    int_to_str(2000+year,sy);
    term_puts("  Time: ", COL_LGREEN);
    term_puts(sh, COL_WHITE); term_puts(":", COL_GREY);
    term_puts(sm, COL_WHITE); term_puts(":", COL_GREY);
    term_putln(ss, COL_WHITE);
    term_puts("  Date: ", COL_LGREEN);
    term_puts(sd, COL_WHITE); term_puts("/", COL_GREY);
    term_puts(smo, COL_WHITE); term_puts("/", COL_GREY);
    term_putln(sy, COL_WHITE);
}

void shell_ram() {
    term_putln("  Memory Map:", COL_LGREEN);
    term_putln("  0x00000  BIOS + IVT", COL_GREY);
    term_putln("  0x01000  QRTOS Kernel", COL_LBLUE);
    term_putln("  0x03000  Free memory", COL_LGREEN);
    term_putln("  0x7FF00  Stack", COL_LRED);
    term_putln("  0xA0000  VGA graphics buffer", COL_LCYAN);
    term_putln("  0xB8000  VGA text buffer", COL_YELLOW);
}

void shell_cpu() {
    unsigned int eax,ebx,ecx,edx;
    char vendor[13];
    __asm__ volatile("cpuid"
        : "=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx) : "a"(0));
    vendor[0]=(ebx>>0)&0xFF;  vendor[1]=(ebx>>8)&0xFF;
    vendor[2]=(ebx>>16)&0xFF; vendor[3]=(ebx>>24)&0xFF;
    vendor[4]=(edx>>0)&0xFF;  vendor[5]=(edx>>8)&0xFF;
    vendor[6]=(edx>>16)&0xFF; vendor[7]=(edx>>24)&0xFF;
    vendor[8]=(ecx>>0)&0xFF;  vendor[9]=(ecx>>8)&0xFF;
    vendor[10]=(ecx>>16)&0xFF;vendor[11]=(ecx>>24)&0xFF;
    vendor[12]='\0';
    term_puts("  Vendor: ", COL_LGREEN); term_putln(vendor, COL_WHITE);
    __asm__ volatile("cpuid"
        : "=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx) : "a"(1));
    term_putln("  Mode: 32-bit Protected", COL_WHITE);
    term_puts("  Features: ", COL_LGREEN);
    if (edx&(1<<25)) term_puts("SSE ", COL_LCYAN);
    if (edx&(1<<26)) term_puts("SSE2 ", COL_LCYAN);
    if (ecx&(1<<0))  term_puts("SSE3 ", COL_LCYAN);
    if (edx&(1<<4))  term_puts("TSC ", COL_LCYAN);
    if (edx&(1<<23)) term_puts("MMX ", COL_LCYAN);
    term_putc('\n', COL_WHITE);
}

void shell_taskmgr() {
    char buf[16];
    term_putln("  PID  Name          State    CPU%", COL_LGREEN);
    int i;
    for (i = 0; i < MAX_PROC; i++) {
        term_puts("  ", COL_WHITE);
        int_to_str(i, buf); term_puts(buf, COL_LCYAN);
        term_puts("    ", COL_WHITE);
        int j = 0;
        while (proc_names[i][j]) { term_putc(proc_names[i][j], COL_WHITE); j++; }
        for (; j < 14; j++) term_putc(' ', COL_WHITE);
        if (proc_state[i]) term_puts("Running  ", COL_LGREEN);
        else               term_puts("Stopped  ", COL_LRED);
        int_to_str(proc_cpu[i], buf);
        term_puts(buf, COL_YELLOW); term_putln("%", COL_YELLOW);
    }
}

void shell_color() {
    int i; char buf[4];
    term_putln("  Color palette:", COL_WHITE);
    for (i = 0; i < 16; i++) {
        int_to_str(i, buf);
        term_puts("  Color ", i);
        term_putln(buf, i);
    }
}

void shell_echo(const char* text) {
    term_putln(text, COL_WHITE);
}

void shell_calc(const char* expr) {
    int i=0; char op=0;
    char num1[32]; char num2[32];
    int n1=0; int n2=0;
    while (expr[i]==' ') i++;
    if (expr[i]=='-') num1[n1++]=expr[i++];
    while (expr[i]>='0'&&expr[i]<='9') num1[n1++]=expr[i++];
    num1[n1]='\0';
    while (expr[i]==' ') i++;
    if (expr[i]=='+'||expr[i]=='-'||
        expr[i]=='*'||expr[i]=='/') { op=expr[i++]; }
    else { term_putln("  Error: invalid expression", COL_LRED); return; }
    while (expr[i]==' ') i++;
    if (expr[i]=='-') num2[n2++]=expr[i++];
    while (expr[i]>='0'&&expr[i]<='9') num2[n2++]=expr[i++];
    num2[n2]='\0';
    if (n1==0||n2==0) { term_putln("  Error: missing number", COL_LRED); return; }
    int a=str_to_int(num1); int b=str_to_int(num2);
    int result=0;
    if      (op=='+') result=a+b;
    else if (op=='-') result=a-b;
    else if (op=='*') result=a*b;
    else if (op=='/') {
        if (b==0) { term_putln("  Error: division by zero!", COL_LRED); return; }
        result=a/b;
    }
    char sa[32]; char sb[32]; char sr[32];
    int_to_str(a,sa); int_to_str(b,sb); int_to_str(result,sr);
    term_puts("  ", COL_WHITE);
    term_puts(sa, COL_LCYAN); term_putc(op, COL_WHITE);
    term_puts(sb, COL_LCYAN); term_puts(" = ", COL_WHITE);
    term_putln(sr, COL_LGREEN);
}

// ── FAS32Q commands ────────────────────────
void shell_ls() {
    int i; int count = 0;
    term_putln("  Files:", COL_LGREEN);
    for (i = 0; i < FAS32Q_MAX_FILES; i++) {
        if (ram_files[i].flags == 0) continue;
        if (ram_files[i].flags & FAS32Q_FLAG_DELETED) continue;
        term_puts("  ", COL_WHITE);
        if (ram_files[i].flags & FAS32Q_FLAG_FOLDER)
            term_puts("[DIR]  ", COL_LCYAN);
        else
            term_puts("[FILE] ", COL_LGREEN);
        term_putln(ram_files[i].name, COL_WHITE);
        count++;
    }
    if (count == 0) term_putln("  No files yet.", COL_GREY);
}

void shell_mkdir(const char* name) {
    int r = fas32q_ram_create(name, FAS32Q_FLAG_FOLDER);
    if (r == -1) term_putln("  Error: could not create folder!", COL_LRED);
    else { term_puts("  Created: ", COL_LGREEN); term_putln(name, COL_WHITE); }
}

void shell_touch(const char* name) {
    int r = fas32q_ram_create(name, FAS32Q_FLAG_FILE);
    if (r == -1) term_putln("  Error: could not create file!", COL_LRED);
    else { term_puts("  Created: ", COL_LGREEN); term_putln(name, COL_WHITE); }
}

void shell_del(const char* name) {
    int r = fas32q_ram_delete(name);
    if (r == -1) term_putln("  Error: file not found!", COL_LRED);
    else { term_puts("  Deleted: ", COL_LRED); term_putln(name, COL_WHITE); }
}

void shell_recycle() {
    int i; int count = 0;
    term_putln("  Recycle Bin:", COL_LRED);
    for (i = 0; i < FAS32Q_MAX_FILES; i++) {
        if (!(ram_files[i].flags & FAS32Q_FLAG_DELETED)) continue;
        term_puts("  ", COL_WHITE);
        term_putln(ram_files[i].name, COL_LRED);
        count++;
    }
    if (count == 0) term_putln("  Recycle bin is empty.", COL_GREY);
}

void shell_restore(const char* name) {
    int r = fas32q_ram_restore(name);
    if (r == -1) term_putln("  Error: not found in recycle bin!", COL_LRED);
    else { term_puts("  Restored: ", COL_LGREEN); term_putln(name, COL_WHITE); }
}

// ── run_command ────────────────────────────
void run_command(char* cmd) {
    if (cmd[0]=='h'&&cmd[1]=='e'&&cmd[2]=='l'&&cmd[3]=='p'&&cmd[4]=='2') { shell_help2(); return; }
    if (cmd[0]=='h'&&cmd[1]=='e'&&cmd[2]=='l'&&cmd[3]=='p'&&cmd[4]=='\0') { shell_help(); return; }
    if (cmd[0]=='c'&&cmd[1]=='l'&&cmd[2]=='e'&&cmd[3]=='a'&&cmd[4]=='r') { term_clear(); return; }
    if (cmd[0]=='r'&&cmd[1]=='a'&&cmd[2]=='m'&&cmd[3]=='\0') { shell_ram(); return; }
    if (cmd[0]=='c'&&cmd[1]=='p'&&cmd[2]=='u'&&cmd[3]=='\0') { shell_cpu(); return; }
    if (cmd[0]=='c'&&cmd[1]=='l'&&cmd[2]=='o'&&cmd[3]=='c'&&cmd[4]=='k') { shell_clock(); return; }
    if (cmd[0]=='c'&&cmd[1]=='a'&&cmd[2]=='l'&&cmd[3]=='c'&&cmd[4]==' ') { shell_calc(cmd+5); return; }
    if (cmd[0]=='e'&&cmd[1]=='c'&&cmd[2]=='h'&&cmd[3]=='o'&&cmd[4]==' ') { shell_echo(cmd+5); return; }
    if (cmd[0]=='s'&&cmd[1]=='y'&&cmd[2]=='s') { shell_sysinfo(); return; }
    if (cmd[0]=='t'&&cmd[1]=='a'&&cmd[2]=='s') { shell_taskmgr(); return; }
    if (cmd[0]=='v'&&cmd[1]=='e'&&cmd[2]=='r') { shell_version(); return; }
    if (cmd[0]=='q'&&cmd[1]=='s'&&cmd[2]=='f'&&cmd[3]=='\0') { shell_qsf(); return; }
    if (cmd[0]=='c'&&cmd[1]=='o'&&cmd[2]=='l') { shell_color(); return; }
    if (cmd[0]=='r'&&cmd[1]=='e'&&cmd[2]=='b') { shell_reboot(); return; }
    if (cmd[0]=='l'&&cmd[1]=='s'&&cmd[2]=='\0') { shell_ls(); return; }
    if (cmd[0]=='m'&&cmd[1]=='k'&&cmd[2]=='d') { shell_mkdir(cmd+6); return; }
    if (cmd[0]=='t'&&cmd[1]=='o'&&cmd[2]=='u') { shell_touch(cmd+6); return; }
    if (cmd[0]=='d'&&cmd[1]=='e'&&cmd[2]=='l') { shell_del(cmd+4); return; }
    if (cmd[0]=='r'&&cmd[1]=='e'&&cmd[2]=='c') { shell_recycle(); return; }
    if (cmd[0]=='r'&&cmd[1]=='e'&&cmd[2]=='s') { shell_restore(cmd+8); return; }
    term_puts("  Unknown: ", COL_LRED);
    term_putln(cmd, COL_WHITE);
    term_putln("  Type 'help' for commands.", COL_GREY);
}

// ── Kernel entry ──────────────────────--

void kernel_main() {
    gfx_init();
 
    // boot screen
    ui_draw_boot_screen();
 
    // delay so user can read boot screen
    volatile unsigned int d;
    for (d = 0; d < 400000000; d++);
 
    // clear screen and start shell
    gfx_clear(0);
    term_init();
    term_cx = 0;
    term_cy = 0;
 
    // enable keyboard
    keyboard_init();
 
    // show prompt
    ui_draw_taskbar();
    term_puts("C:\\QRTOS> ", COL_LGREEN);
 
    while (1) {
        char c = read_key();
        if (!c) continue;
 
        if (c == '\n') {
            input_buf[input_len] = '\0';
            term_putc('\n', COL_WHITE);
            if (input_len > 0) run_command(input_buf);
            input_len = 0;
            ui_draw_taskbar();
            term_puts("C:\\QRTOS> ", COL_LGREEN);
        } else if (c == '\b') {
            if (input_len > 0) {
                input_len--;
                term_putc('\b', COL_WHITE);
            }
        } else {
            if (input_len < 255) {
                input_buf[input_len++] = c;
                term_putc(c, COL_WHITE);
            }
        }
    }
}
 
