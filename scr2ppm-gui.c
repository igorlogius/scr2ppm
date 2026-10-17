/**
 * description: small Xlib window to choose what to capture (screen / window / area)
 *              and an optional delay, then starts scr2ppm-wrapper with those options
 * repo: https://github.com/igorlogius/scr2ppm
 * author: https://github.com/igorlogius
 *
 * The window closes itself before the capture starts, so it never ends up in
 * the screenshot. Only plain Xlib is used, no toolkit.
 **/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/wait.h>
#include<sys/stat.h>
#include<sys/types.h>

#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include<X11/Xatom.h>
#include<X11/cursorfont.h>
#include<X11/keysym.h>
#include<X11/Xft/Xft.h>

#include "scr2ppm-icon.h"

#define WIN_W      360
#define WIN_H      510
#define MAX_DELAY  60

enum { MODE_SCREEN, MODE_WINDOW, MODE_AREA, MODE_COUNT };
enum { FMT_PPM, FMT_PNG, FMT_JPG, FMT_COUNT };
enum { HIT_NONE, HIT_MODE0, HIT_MODE1, HIT_MODE2, HIT_MINUS, HIT_PLUS, HIT_SAVE, HIT_FMT0, HIT_FMT1, HIT_FMT2, HIT_OPENDIR, HIT_CHGDIR, HIT_DEFDIR, HIT_COPY, HIT_SHOW, HIT_REOPEN, HIT_CANCEL, HIT_CAPTURE };

typedef struct { int x, y, w, h, id; } Hit;

static const char *fmt_label[FMT_COUNT] = { "PPM", "PNG", "JPG" };
static const char *fmt_arg[FMT_COUNT]   = { "ppm", "png", "jpg" };
static const char *mode_arg[MODE_COUNT] = { "screen", "window", "area" };

static const char *mode_label[MODE_COUNT] = {
    "Entire screen",
    "A window (click on it)",
    "An area (drag, hold Space to move)"
};

/* clickable regions, also used for drawing (rows of the two "cards" are clickable as a whole) */
static const Hit hits[] = {
    {  16,  34, 328, 34, HIT_MODE0   },
    {  16,  68, 328, 34, HIT_MODE1   },
    {  16, 102, 328, 34, HIT_MODE2   },
    { 186, 176,  30, 28, HIT_MINUS   },
    { 302, 176,  30, 28, HIT_PLUS    },
    {  16, 210, 328, 34, HIT_SAVE    },
    { 146, 250,  62, 28, HIT_FMT0    },
    { 208, 250,  62, 28, HIT_FMT1    },
    { 270, 250,  62, 28, HIT_FMT2    },
    { 132, 290,  52, 28, HIT_OPENDIR },
    { 190, 290,  72, 28, HIT_CHGDIR  },
    { 268, 290,  64, 28, HIT_DEFDIR  },
    {  16, 348, 328, 34, HIT_COPY    },
    {  16, 382, 328, 34, HIT_SHOW    },
    {  16, 416, 328, 34, HIT_REOPEN  },
    {  16, 466,  76, 32, HIT_CANCEL  },
    { 254, 466,  90, 32, HIT_CAPTURE },
};
#define N_HITS ((int)(sizeof(hits) / sizeof(hits[0])))

static Display *disp;
static Window win;
static Pixmap buf;        /* everything is drawn here first, then copied to the window (no flicker) */
static GC gc;
static XftFont *font;     /* Xft: anti-aliased, fontconfig-configured text */
static XftFont *font_small;
static XftDraw *xdraw;
static XftColor xc_fg, xc_white, xc_muted;

static unsigned long col_bg, col_fg, col_border, col_accent, col_white;
static unsigned long col_card, col_sep, col_track;
static unsigned long col_btn, col_btn_hover, col_btn_press;
static unsigned long col_accent_hover, col_accent_press;

static int mode = MODE_AREA;
static int delay = 0;
static int format = FMT_PPM;
static char save_dir[1024];   /* empty = the default Pictures folder (passed on as -D) */
static int do_save = 1;   /* save the screenshot to a file */
static int do_copy = 0;   /* copy the screenshot to the clipboard (as PNG) */
static int do_show = 0;   /* open the saved screenshot afterwards (needs do_save) */
static int reopen = 0;   /* re-open this window after a capture (passed on as -r) */
static int pressed = HIT_NONE;
static int hover = HIT_NONE;
static Cursor cur_hand;

static unsigned long
allocColor(const char *name, unsigned long fallback){
    int scr = DefaultScreen(disp);
    XColor c, exact;
    if (XAllocNamedColor(disp, DefaultColormap(disp, scr), name, &c, &exact)) {
        return c.pixel;
    }
    return fallback;
}

/* "sans" is resolved by fontconfig, so the user's antialiasing/hinting/DPI settings apply */
static XftFont *
loadFont(void){
    const char *names[] = { "sans:size=10", "DejaVu Sans:size=10", "monospace:size=10", NULL };
    int scr = DefaultScreen(disp);
    for (int i = 0; names[i]; i++) {
        XftFont *f = XftFontOpenName(disp, scr, names[i]);
        if (f) { return f; }
    }
    return NULL;
}

static void
allocXftColor(XftColor *c, unsigned short r, unsigned short g, unsigned short b){
    XRenderColor rc = { r, g, b, 0xffff };
    int scr = DefaultScreen(disp);
    XftColorAllocValue(disp, DefaultVisual(disp, scr), DefaultColormap(disp, scr), &rc, c);
}

static int
textWidth(const char *s){
    XGlyphInfo ext;
    XftTextExtentsUtf8(disp, font, (const FcChar8 *)s, (int)strlen(s), &ext);
    return ext.xOff;
}

/* copy s into out, cutting off the start ("…/rest/of/path") if it is wider than maxw */
static void
fitText(const char *s, int maxw, char *out, size_t n){
    const char *p = s;
    if (textWidth(s) <= maxw) { snprintf(out, n, "%s", s); return; }
    while (*p) {
        p++;
        while (((unsigned char)*p & 0xC0) == 0x80) { p++; }   /* stay on a UTF-8 boundary */
        snprintf(out, n, "\xE2\x80\xA6%s", p);
        if (textWidth(out) <= maxw) { return; }
    }
    snprintf(out, n, "\xE2\x80\xA6");
}

static const Hit *
findHit(int id){
    for (int i = 0; i < N_HITS; i++) { if (hits[i].id == id) { return &hits[i]; } }
    return NULL;
}

static int
hitAt(int x, int y){
    for (int i = 0; i < N_HITS; i++) {
        const Hit *h = &hits[i];
        if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) { return h->id; }
    }
    return HIT_NONE;
}

static void
drawTextF(XftFont *f, int x, int baseline, const char *s, const XftColor *color){
    XftDrawStringUtf8(xdraw, color, f, x, baseline, (const FcChar8 *)s, (int)strlen(s));
}

static void
drawText(int x, int baseline, const char *s, const XftColor *color){
    drawTextF(font, x, baseline, s, color);
}

/* baseline that vertically centers text inside a box of height h at y */
static int
centerBaseline(int y, int h){
    return y + (h + font->ascent - font->descent) / 2;
}

static void
fillRect(int x, int y, int w, int h, unsigned long color){
    XSetForeground(disp, gc, color);
    XFillRectangle(disp, buf, gc, x, y, w, h);
}

static void
fillCircle(int x, int y, int d, unsigned long color){
    XSetForeground(disp, gc, color);
    XFillArc(disp, buf, gc, x, y, d, d, 0, 360 * 64);
}

static void
fillRound(int x, int y, int w, int h, int r, unsigned long color){
    if (r * 2 > h) { r = h / 2; }
    if (r * 2 > w) { r = w / 2; }
    if (r <= 0) { fillRect(x, y, w, h, color); return; }
    fillRect(x + r, y, w - 2 * r, h, color);
    fillRect(x, y + r, w, h - 2 * r, color);
    fillCircle(x, y, 2 * r, color);
    fillCircle(x + w - 2 * r, y, 2 * r, color);
    fillCircle(x, y + h - 2 * r, 2 * r, color);
    fillCircle(x + w - 2 * r, y + h - 2 * r, 2 * r, color);
}

/* rounded box with a 1px border */
static void
roundBox(int x, int y, int w, int h, int r, unsigned long face, unsigned long border){
    fillRound(x, y, w, h, r, border);
    fillRound(x + 1, y + 1, w - 2, h - 2, r - 1, face);
}

static void
drawCard(int x, int y, int w, int h){
    roundBox(x, y, w, h, 10, col_card, col_border);
}

static void
drawSeparator(int y){
    fillRect(28, y, 304, 1, col_sep);
}

static void
drawButton(const Hit *h, const char *label, int primary, int enabled){
    int is_pressed = enabled && pressed == h->id;
    int is_hover   = enabled && hover == h->id;
    unsigned long face, edge;

    if (primary && !enabled) {
        face = col_track;
        edge = col_border;
    } else if (primary) {
        face = is_pressed ? col_accent_press : is_hover ? col_accent_hover : col_accent;
        edge = face;
    } else {
        face = is_pressed ? col_btn_press : is_hover ? col_btn_hover : col_btn;
        edge = col_border;
    }
    roundBox(h->x, h->y, h->w, h->h, 7, face, edge);

    int tw = textWidth(label);
    const XftColor *text = !enabled ? &xc_muted : primary ? &xc_white : &xc_fg;
    drawText(h->x + (h->w - tw) / 2, centerBaseline(h->y, h->h), label, text);
}

/* checkbox row (the whole row is clickable); a disabled one is greyed out and shown unchecked */
static void
drawCheck(int id, const char *label, int on, int enabled){
    const Hit *h = findHit(id);
    int cy = h->y + (h->h - 16) / 2;
    int hov = enabled && hover == id;
    unsigned long face = on ? (enabled ? col_accent : col_border) : col_card;
    unsigned long edge = on ? face : (hov ? col_accent : col_border);

    roundBox(30, cy, 16, 16, 4, face, edge);
    if (on) {
        XSetForeground(disp, gc, col_white);
        XSetLineAttributes(disp, gc, 2, LineSolid, CapRound, JoinRound);
        XDrawLine(disp, buf, gc, 34, cy + 8, 37, cy + 11);
        XDrawLine(disp, buf, gc, 37, cy + 11, 42, cy + 5);
        XSetLineAttributes(disp, gc, 0, LineSolid, CapButt, JoinMiter);
    }
    drawText(56, centerBaseline(h->y, h->h), label, enabled ? &xc_fg : &xc_muted);
}

static int
canCapture(void){
    return do_save || do_copy;
}

static void
draw(void){
    char tbuf[16];

    fillRect(0, 0, WIN_W, WIN_H, col_bg);

    /* ---- what to capture ---- */
    drawTextF(font_small, 20, 26, "WHAT TO CAPTURE", &xc_muted);
    drawCard(16, 34, 328, 102);
    drawSeparator(68);
    drawSeparator(102);
    for (int i = 0; i < MODE_COUNT; i++) {
        const Hit *h = findHit(HIT_MODE0 + i);
        int cy = h->y + (h->h - 16) / 2;
        if (mode == i) {
            fillCircle(30, cy, 16, col_accent);
            fillCircle(35, cy + 5, 6, col_white);
        } else {
            fillCircle(30, cy, 16, (hover == h->id) ? col_accent : col_border);
            fillCircle(31, cy + 1, 14, col_card);
        }
        drawText(56, centerBaseline(h->y, h->h), mode_label[i], &xc_fg);
    }

    /* ---- options ---- */
    drawTextF(font_small, 20, 162, "OPTIONS", &xc_muted);
    drawCard(16, 170, 328, 280);
    drawSeparator(210);
    drawSeparator(244);
    drawSeparator(284);
    drawSeparator(348);
    drawSeparator(382);
    drawSeparator(416);

    /* delay row: label on the left, [-] value [+] on the right */
    {
        const Hit *minus = findHit(HIT_MINUS);
        const Hit *plus  = findHit(HIT_PLUS);
        drawText(28, centerBaseline(170, 40), "Delay", &xc_fg);
        drawButton(minus, "\xE2\x88\x92", 0, delay > 0);
        drawButton(plus, "+", 0, delay < MAX_DELAY);

        snprintf(tbuf, sizeof(tbuf), delay == 0 ? "none" : "%d s", delay);
        int vx0 = minus->x + minus->w;
        int vw  = plus->x - vx0;
        drawText(vx0 + (vw - textWidth(tbuf)) / 2, centerBaseline(minus->y, minus->h), tbuf, &xc_fg);
    }

    drawCheck(HIT_SAVE, "Save to file", do_save, 1);

    /* format row (only used when saving): segmented control, the selected format is the highlighted segment */
    {
        const Hit *f0 = findHit(HIT_FMT0);
        drawText(28, centerBaseline(244, 40), "Format", do_save ? &xc_fg : &xc_muted);
        fillRound(f0->x, f0->y, FMT_COUNT * f0->w, f0->h, 8, col_track);
        for (int i = 0; i < FMT_COUNT; i++) {
            const Hit *h = findHit(HIT_FMT0 + i);
            int sel = (format == i);
            if (sel) {
                fillRound(h->x + 2, h->y + 2, h->w - 4, h->h - 4, 6,
                          !do_save ? col_border :
                          pressed == h->id ? col_accent_press : hover == h->id ? col_accent_hover : col_accent);
            } else if (do_save && (hover == h->id || pressed == h->id)) {
                fillRound(h->x + 2, h->y + 2, h->w - 4, h->h - 4, 6,
                          pressed == h->id ? col_btn_press : col_btn_hover);
            }
            drawText(h->x + (h->w - textWidth(fmt_label[i])) / 2, centerBaseline(h->y, h->h),
                     fmt_label[i], (sel && do_save) ? &xc_white : do_save ? &xc_fg : &xc_muted);
        }
    }

    /* folder row (only used when saving): label + open / change / default buttons, the current folder below */
    {
        const Hit *o = findHit(HIT_OPENDIR);
        char pbuf[1100];
        drawText(28, centerBaseline(o->y, o->h), "Folder", do_save ? &xc_fg : &xc_muted);
        drawButton(o, "Open", 0, do_save);
        drawButton(findHit(HIT_CHGDIR), "Change\xE2\x80\xA6", 0, do_save);
        drawButton(findHit(HIT_DEFDIR), "Default", 0, do_save && save_dir[0] != '\0');
        fitText(save_dir[0] ? save_dir : "Default (your Pictures folder)", 304, pbuf, sizeof(pbuf));
        drawText(28, o->y + o->h + 17, pbuf, &xc_muted);
    }

    drawCheck(HIT_COPY, "Copy to clipboard", do_copy, 1);
    drawCheck(HIT_SHOW, "Show screenshot after capture", do_show && do_save, do_save);
    drawCheck(HIT_REOPEN, "Re-open this window after capture", reopen, 1);

    /* ---- action buttons: Capture needs at least one of save / copy ---- */
    drawButton(findHit(HIT_CANCEL), "Cancel", 0, 1);
    drawButton(findHit(HIT_CAPTURE), "Capture", 1, canCapture());
    if (!canCapture()) {
        const char *hint = "Select save or copy";
        drawText(100 + (146 - textWidth(hint)) / 2, centerBaseline(466, 32), hint, &xc_muted);
    }

    XCopyArea(disp, buf, win, gc, 0, 0, WIN_W, WIN_H, 0, 0);
    XFlush(disp);
}

/* the capture mode, format, delay and re-open option are remembered in $XDG_CONFIG_HOME/scr2ppm/gui.conf
 * ("mode=area", "format=png", "delay=5", "reopen=1", "save=1", "copy=0", "show=0", "dir=/path/to/folder") */
static int
configPath(char *buf, size_t len, int create_dir){
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char base[512];

    if (xdg && xdg[0]) {
        snprintf(base, sizeof(base), "%s", xdg);
    } else if (home && home[0]) {
        snprintf(base, sizeof(base), "%s/.config", home);
    } else {
        return 0;
    }
    if (create_dir) {
        mkdir(base, 0755);
        snprintf(buf, len, "%s/scr2ppm", base);
        mkdir(buf, 0755);
    }
    snprintf(buf, len, "%s/scr2ppm/gui.conf", base);
    return 1;
}

static void
loadConfig(void){
    char path[600], line[1100];
    FILE *f;

    if (!configPath(path, sizeof(path), 0) || (f = fopen(path, "r")) == NULL) { return; }
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "format=", 7) == 0) {
            for (int i = 0; i < FMT_COUNT; i++) {
                if (strcmp(line + 7, fmt_arg[i]) == 0) { format = i; }
            }
        } else if (strncmp(line, "mode=", 5) == 0) {
            for (int i = 0; i < MODE_COUNT; i++) {
                if (strcmp(line + 5, mode_arg[i]) == 0) { mode = i; }
            }
        } else if (strncmp(line, "dir=", 4) == 0) {
            snprintf(save_dir, sizeof(save_dir), "%s", line + 4);
        } else if (strncmp(line, "save=", 5) == 0) {
            do_save = (atoi(line + 5) != 0);
        } else if (strncmp(line, "copy=", 5) == 0) {
            do_copy = (atoi(line + 5) != 0);
        } else if (strncmp(line, "show=", 5) == 0) {
            do_show = (atoi(line + 5) != 0);
        } else if (strncmp(line, "reopen=", 7) == 0) {
            reopen = (atoi(line + 7) != 0);
        } else if (strncmp(line, "delay=", 6) == 0) {
            int d = atoi(line + 6);
            delay = (d < 0) ? 0 : (d > MAX_DELAY) ? MAX_DELAY : d;
        }
    }
    fclose(f);
    if (!do_save && !do_copy) { do_save = 1; }   /* a capture needs at least one destination */
}

static void
saveConfig(void){
    char path[600];
    FILE *f;

    if (!configPath(path, sizeof(path), 1) || (f = fopen(path, "w")) == NULL) { return; }
    fprintf(f, "mode=%s\nformat=%s\ndelay=%d\nreopen=%d\nsave=%d\ncopy=%d\nshow=%d\ndir=%s\n",
            mode_arg[mode], fmt_arg[format], delay, reopen, do_save, do_copy, do_show, save_dir);
    fclose(f);
}

static void
setMode(int m){
    if (m != mode) {
        mode = m;
        saveConfig();
    }
}

static void
setReopen(int r){
    if (r != reopen) {
        reopen = r;
        saveConfig();
    }
}

static void
setSave(int v){
    if (v != do_save) { do_save = v; saveConfig(); }
}

static void
setCopy(int v){
    if (v != do_copy) { do_copy = v; saveConfig(); }
}

static void
setShow(int v){
    if (v != do_show) { do_show = v; saveConfig(); }
}

static void
setFormat(int f){
    if (f != format) {
        format = f;
        saveConfig();
    }
}

static void
setDelay(int d){
    if (d < 0) { d = 0; }
    if (d > MAX_DELAY) { d = MAX_DELAY; }
    if (d != delay) {
        delay = d;
        saveConfig();
    }
}

/* close the window, give the compositor time to remove it, then run the wrapper with
 * the chosen options: -n (don't save), -c (copy to clipboard), -o (open the saved screenshot) */
static void
launch(void){
    char dbuf[16];
    char *argv[14];
    int n = 0;

    argv[n++] = "scr2ppm-wrapper";
    argv[n++] = (mode == MODE_SCREEN) ? "-s" : (mode == MODE_WINDOW) ? "-w" : "-a";
    if (delay > 0) {
        snprintf(dbuf, sizeof(dbuf), "%d", delay);
        argv[n++] = "-d";
        argv[n++] = dbuf;
    }
    if (do_save) {
        if (format != FMT_PPM) {
            argv[n++] = "-f";
            argv[n++] = (char *)fmt_arg[format];
        }
        if (save_dir[0]) {
            argv[n++] = "-D";
            argv[n++] = save_dir;
        }
        if (do_show) { argv[n++] = "-o"; }
    } else {
        argv[n++] = "-n";
    }
    if (do_copy) { argv[n++] = "-c"; }
    if (reopen) { argv[n++] = "-r"; }
    argv[n] = NULL;

    XUnmapWindow(disp, win);
    XDestroyWindow(disp, win);
    XSync(disp, False);
    XCloseDisplay(disp);
    usleep(300000);

    execvp(argv[0], argv);
    perror("scr2ppm-gui: failed to start scr2ppm-wrapper");
    exit(1);
}

/* run a command without waiting for it (double fork, so no zombie is left behind) */
static void
spawnDetached(char *const argv[]){
    pid_t pid = fork();
    if (pid < 0) { return; }
    if (pid == 0) {
        if (fork() != 0) { _exit(0); }
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); }
        execvp(argv[0], argv);
        _exit(127);
    }
    waitpid(pid, NULL, 0);
}

static void
openFolder(void){
    char *argv[5];
    int n = 0;
    argv[n++] = "scr2ppm-wrapper";
    argv[n++] = "--open-dir";
    if (save_dir[0]) {
        argv[n++] = "-D";
        argv[n++] = save_dir;
    }
    argv[n] = NULL;
    spawnDetached(argv);
}

/* the chooser runs in the wrapper (portal, zenity or kdialog); this window waits for it */
static void
chooseFolder(void){
    char buf[sizeof(save_dir)];
    FILE *fp = popen("scr2ppm-wrapper --choose-dir", "r");
    if (fp == NULL) { return; }
    if (fgets(buf, sizeof(buf), fp) != NULL) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (buf[0]) {
            snprintf(save_dir, sizeof(save_dir), "%s", buf);
            saveConfig();
        }
    }
    pclose(fp);
}

static void
resetFolder(void){
    if (save_dir[0]) {
        save_dir[0] = '\0';
        saveConfig();
    }
}

static void
activate(int id){
    switch (id) {
        case HIT_MODE0:
        case HIT_MODE1:
        case HIT_MODE2:   setMode(id - HIT_MODE0); break;
        case HIT_SAVE:    setSave(!do_save);      break;
        case HIT_COPY:    setCopy(!do_copy);      break;
        case HIT_SHOW:    if (do_save) { setShow(!do_show); } break;
        case HIT_FMT0:
        case HIT_FMT1:
        case HIT_FMT2:    if (do_save) { setFormat(id - HIT_FMT0); } break;
        case HIT_OPENDIR: if (do_save) { openFolder(); }   break;
        case HIT_CHGDIR:  if (do_save) { chooseFolder(); } break;
        case HIT_DEFDIR:  if (do_save) { resetFolder(); }  break;
        case HIT_REOPEN:  setReopen(!reopen);    break;
        case HIT_MINUS:   setDelay(delay - 1);    break;
        case HIT_PLUS:    setDelay(delay + 1);    break;
        case HIT_CANCEL:  XCloseDisplay(disp); exit(0);
        case HIT_CAPTURE: if (canCapture()) { launch(); } break;
        default: break;
    }
}

static void
onKey(XKeyEvent *ke){
    KeySym ks = XLookupKeysym(ke, 0);
    switch (ks) {
        case XK_Escape:                       XCloseDisplay(disp); exit(0);
        case XK_Return: case XK_KP_Enter:     if (canCapture()) { launch(); } break;
        case XK_s: case XK_1:                 setMode(MODE_SCREEN); break;
        case XK_w: case XK_2:                 setMode(MODE_WINDOW); break;
        case XK_a: case XK_3:                 setMode(MODE_AREA);   break;
        case XK_f:                            setSave(!do_save);    break;
        case XK_y:                            setCopy(!do_copy);    break;
        case XK_v:                            if (do_save) { setShow(!do_show); } break;
        case XK_r:                            setReopen(!reopen);   break;
        case XK_o:                            if (do_save) { openFolder(); } return;
        case XK_c:                            if (do_save) { chooseFolder(); } break;
        case XK_p:                            if (do_save) { setFormat((format + 1) % FMT_COUNT); } break;
        case XK_Up: case XK_plus: case XK_equal: case XK_KP_Add:
            setDelay(delay + 1); break;
        case XK_Down: case XK_minus: case XK_KP_Subtract:
            setDelay(delay - 1); break;
        default: return;
    }
    draw();
}

int
main(int argc, char *argv[]){
    /* -r: started by the wrapper after a capture, so the re-open option must be on */
    int force_reopen = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) { force_reopen = 1; }
    }

    disp = XOpenDisplay(NULL);
    if (disp == NULL) {
        fprintf(stderr, "scr2ppm-gui: failed to open display\n");
        return 1;
    }
    int scr = DefaultScreen(disp);

    loadConfig();
    if (force_reopen) { reopen = 1; }

    font = loadFont();
    if (font == NULL) {
        fprintf(stderr, "scr2ppm-gui: no usable font found\n");
        return 1;
    }

    col_bg           = allocColor("#efeeec", WhitePixel(disp, scr));
    col_fg           = allocColor("#2e3436", BlackPixel(disp, scr));
    col_border       = allocColor("#cdcbc7", BlackPixel(disp, scr));
    col_accent       = allocColor("#3584e4", BlackPixel(disp, scr));
    col_accent_hover = allocColor("#4f94e8", BlackPixel(disp, scr));
    col_accent_press = allocColor("#1c6fd6", BlackPixel(disp, scr));
    col_white        = allocColor("#ffffff", WhitePixel(disp, scr));
    col_card         = allocColor("#ffffff", WhitePixel(disp, scr));
    col_sep          = allocColor("#e9e8e6", WhitePixel(disp, scr));
    col_track        = allocColor("#e4e2df", WhitePixel(disp, scr));
    col_btn          = allocColor("#f8f7f6", WhitePixel(disp, scr));
    col_btn_hover    = allocColor("#ecebe9", WhitePixel(disp, scr));
    col_btn_press    = allocColor("#dcdad6", WhitePixel(disp, scr));

    font_small = XftFontOpenName(disp, scr, "sans:bold:size=8");
    if (font_small == NULL) { font_small = font; }
    cur_hand = XCreateFontCursor(disp, XC_hand2);

    int sw = DisplayWidth(disp, scr);
    int sh = DisplayHeight(disp, scr);
    win = XCreateSimpleWindow(disp, RootWindow(disp, scr),
                              (sw - WIN_W) / 2, (sh - WIN_H) / 2, WIN_W, WIN_H,
                              0, col_fg, col_bg);

    XStoreName(disp, win, "Screen to PPM");

    XClassHint *ch = XAllocClassHint();
    if (ch) {
        ch->res_name  = "scr2ppm";
        ch->res_class = "scr2ppm";
        XSetClassHint(disp, win, ch);
        XFree(ch);
    }

    /* window icon (shown in the title bar / taskbar), embedded so it works without an installed icon theme */
    Atom net_wm_icon = XInternAtom(disp, "_NET_WM_ICON", False);
    XChangeProperty(disp, win, net_wm_icon, XA_CARDINAL, 32, PropModeReplace,
                    (const unsigned char *)icon_data,
                    (int)(sizeof(icon_data) / sizeof(icon_data[0])));

    /* fixed size window */
    XSizeHints *sz = XAllocSizeHints();
    if (sz) {
        sz->flags = PMinSize | PMaxSize;
        sz->min_width  = sz->max_width  = WIN_W;
        sz->min_height = sz->max_height = WIN_H;
        XSetWMNormalHints(disp, win, sz);
        XFree(sz);
    }

    Atom wm_delete = XInternAtom(disp, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(disp, win, &wm_delete, 1);

    gc = XCreateGC(disp, win, 0, NULL);
    buf = XCreatePixmap(disp, win, WIN_W, WIN_H, DefaultDepth(disp, scr));
    xdraw = XftDrawCreate(disp, buf, DefaultVisual(disp, scr), DefaultColormap(disp, scr));
    XSetWindowBackgroundPixmap(disp, win, buf);
    allocXftColor(&xc_fg, 0x2e2e, 0x3434, 0x3636);
    allocXftColor(&xc_white, 0xffff, 0xffff, 0xffff);
    allocXftColor(&xc_muted, 0x7777, 0x7676, 0x7b7b);

    XSelectInput(disp, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                 PointerMotionMask | LeaveWindowMask);
    XMapWindow(disp, win);

    XEvent ev;
    for (;;) {
        XNextEvent(disp, &ev);
        switch (ev.type) {
            case Expose:
                if (ev.xexpose.count == 0) { draw(); }
                break;
            case KeyPress:
                onKey(&ev.xkey);
                break;
            case ButtonPress:
                if (ev.xbutton.button == Button1) {
                    pressed = hitAt(ev.xbutton.x, ev.xbutton.y);
                    draw();
                }
                break;
            case ButtonRelease:
                if (ev.xbutton.button == Button1) {
                    int id = hitAt(ev.xbutton.x, ev.xbutton.y);
                    int was = pressed;
                    pressed = HIT_NONE;
                    /* only activate when released on the same widget it was pressed on */
                    if (id != HIT_NONE && id == was) { activate(id); }
                    draw();
                }
                break;
            case MotionNotify:
            case LeaveNotify: {
                int id = (ev.type == LeaveNotify) ? HIT_NONE : hitAt(ev.xmotion.x, ev.xmotion.y);
                if (id != hover) {
                    hover = id;
                    XDefineCursor(disp, win, id != HIT_NONE ? cur_hand : None);
                    draw();
                }
                break;
            }
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wm_delete) {
                    XCloseDisplay(disp);
                    return 0;
                }
                break;
            default:
                break;
        }
    }
}
