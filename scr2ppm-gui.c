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
#include<X11/keysym.h>
#include<X11/Xft/Xft.h>

#define WIN_W      320
#define WIN_H      340
#define MAX_DELAY  60

enum { MODE_SCREEN, MODE_WINDOW, MODE_AREA, MODE_COUNT };
enum { FMT_PPM, FMT_PNG, FMT_JPG, FMT_COUNT };
enum { HIT_NONE, HIT_MODE0, HIT_MODE1, HIT_MODE2, HIT_MINUS, HIT_PLUS, HIT_FMT0, HIT_FMT1, HIT_FMT2, HIT_OPENDIR, HIT_CHGDIR, HIT_DEFDIR, HIT_REOPEN, HIT_CANCEL, HIT_CAPTURE, HIT_CAPTURE_OPEN };

typedef struct { int x, y, w, h, id; } Hit;

static const char *fmt_label[FMT_COUNT] = { "PPM", "PNG", "JPG" };
static const char *fmt_arg[FMT_COUNT]   = { "ppm", "png", "jpg" };
static const char *mode_arg[MODE_COUNT] = { "screen", "window", "area" };

static const char *mode_label[MODE_COUNT] = {
    "Entire screen",
    "A window (click on it)",
    "An area (drag, hold Space to move)"
};

/* clickable regions, also used for drawing */
static const Hit hits[] = {
    {  10,  40, 300, 26, HIT_MODE0   },
    {  10,  68, 300, 26, HIT_MODE1   },
    {  10,  96, 300, 26, HIT_MODE2   },
    { 120, 140,  28, 26, HIT_MINUS   },
    { 196, 140,  28, 26, HIT_PLUS    },
    {  90, 180,  60, 26, HIT_FMT0    },
    { 156, 180,  60, 26, HIT_FMT1    },
    { 222, 180,  60, 26, HIT_FMT2    },
    {  80, 214,  60, 26, HIT_OPENDIR },
    { 146, 214,  80, 26, HIT_CHGDIR  },
    { 232, 214,  78, 26, HIT_DEFDIR  },
    {  10, 268, 300, 22, HIT_REOPEN  },
    {  10, 298,  70, 30, HIT_CANCEL       },
    {  88, 298,  80, 30, HIT_CAPTURE      },
    { 176, 298, 134, 30, HIT_CAPTURE_OPEN },
};
#define N_HITS ((int)(sizeof(hits) / sizeof(hits[0])))

static Display *disp;
static Window win;
static GC gc;
static XftFont *font;     /* Xft: anti-aliased, fontconfig-configured text */
static XftDraw *xdraw;
static XftColor xc_fg, xc_white;

static unsigned long col_bg, col_fg, col_border, col_accent, col_white;

static int mode = MODE_AREA;
static int delay = 0;
static int format = FMT_PPM;
static char save_dir[1024];   /* empty = the default Pictures folder (passed on as -D) */
static int reopen = 0;   /* re-open this window after a capture (passed on as -r) */
static int pressed = HIT_NONE;

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
drawText(int x, int baseline, const char *s, const XftColor *color){
    XftDrawStringUtf8(xdraw, color, font, x, baseline, (const FcChar8 *)s, (int)strlen(s));
}

/* baseline that vertically centers text inside a box of height h at y */
static int
centerBaseline(int y, int h){
    return y + (h + font->ascent - font->descent) / 2;
}

static void
drawButton(const Hit *h, const char *label, int primary){
    int is_pressed = (pressed == h->id);
    unsigned long face = primary ? col_accent : col_bg;
    const XftColor *text = primary ? &xc_white : &xc_fg;
    if (is_pressed) { face = primary ? col_fg : col_border; }

    XSetForeground(disp, gc, face);
    XFillRectangle(disp, win, gc, h->x, h->y, h->w, h->h);
    XSetForeground(disp, gc, primary ? col_accent : col_border);
    XDrawRectangle(disp, win, gc, h->x, h->y, h->w - 1, h->h - 1);

    int tw = textWidth(label);
    drawText(h->x + (h->w - tw) / 2, centerBaseline(h->y, h->h), label, text);
}

static void
draw(void){
    char buf[16];

    XSetForeground(disp, gc, col_bg);
    XFillRectangle(disp, win, gc, 0, 0, WIN_W, WIN_H);

    /* heading */
    XSetForeground(disp, gc, col_fg);
    drawText(20, 28, "What do you want to capture?", &xc_fg);

    /* radio buttons */
    for (int i = 0; i < MODE_COUNT; i++) {
        const Hit *h = findHit(HIT_MODE0 + i);
        int cy = h->y + (h->h - 14) / 2;
        XSetForeground(disp, gc, (mode == i) ? col_accent : col_border);
        XDrawArc(disp, win, gc, 20, cy, 14, 14, 0, 360 * 64);
        if (mode == i) {
            XFillArc(disp, win, gc, 24, cy + 4, 7, 7, 0, 360 * 64);
        }
        XSetForeground(disp, gc, col_fg);
        drawText(44, centerBaseline(h->y, h->h), mode_label[i], &xc_fg);
    }

    /* delay row */
    const Hit *minus = findHit(HIT_MINUS);
    const Hit *plus  = findHit(HIT_PLUS);
    XSetForeground(disp, gc, col_fg);
    drawText(20, centerBaseline(minus->y, minus->h), "Delay:", &xc_fg);
    drawButton(minus, "-", 0);
    drawButton(plus, "+", 0);

    snprintf(buf, sizeof(buf), delay == 0 ? "none" : "%d s", delay);
    int vx0 = minus->x + minus->w;
    int vw  = plus->x - vx0;
    int tw  = textWidth(buf);
    XSetForeground(disp, gc, col_fg);
    drawText(vx0 + (vw - tw) / 2, centerBaseline(minus->y, minus->h), buf, &xc_fg);

    /* format row: the selected format is drawn as the highlighted button */
    XSetForeground(disp, gc, col_fg);
    drawText(20, centerBaseline(findHit(HIT_FMT0)->y, findHit(HIT_FMT0)->h), "Format:", &xc_fg);
    for (int i = 0; i < FMT_COUNT; i++) {
        drawButton(findHit(HIT_FMT0 + i), fmt_label[i], format == i);
    }

    /* folder row: open / change / back to the default, and the current folder below */
    {
        const Hit *o = findHit(HIT_OPENDIR);
        char pbuf[1100];
        XSetForeground(disp, gc, col_fg);
        drawText(20, centerBaseline(o->y, o->h), "Folder:", &xc_fg);
        drawButton(o, "Open", 0);
        drawButton(findHit(HIT_CHGDIR), "Change\xE2\x80\xA6", 0);
        drawButton(findHit(HIT_DEFDIR), "Default", 0);
        fitText(save_dir[0] ? save_dir : "Default (your Pictures folder)", WIN_W - 40, pbuf, sizeof(pbuf));
        drawText(20, o->y + o->h + 16, pbuf, &xc_fg);
    }

    /* re-open checkbox */
    {
        const Hit *h = findHit(HIT_REOPEN);
        int cy = h->y + (h->h - 14) / 2;
        XSetForeground(disp, gc, reopen ? col_accent : col_bg);
        XFillRectangle(disp, win, gc, 20, cy, 14, 14);
        XSetForeground(disp, gc, reopen ? col_accent : col_border);
        XDrawRectangle(disp, win, gc, 20, cy, 14, 14);
        if (reopen) {
            XSetForeground(disp, gc, col_white);
            XSetLineAttributes(disp, gc, 2, LineSolid, CapRound, JoinRound);
            XDrawLine(disp, win, gc, 23, cy + 7, 26, cy + 10);
            XDrawLine(disp, win, gc, 26, cy + 10, 31, cy + 4);
            XSetLineAttributes(disp, gc, 0, LineSolid, CapButt, JoinMiter);
        }
        XSetForeground(disp, gc, col_fg);
        drawText(44, centerBaseline(h->y, h->h), "Re-open this window after capture", &xc_fg);
    }

    /* action buttons */
    drawButton(findHit(HIT_CANCEL), "Cancel", 0);
    drawButton(findHit(HIT_CAPTURE), "Capture", 1);
    drawButton(findHit(HIT_CAPTURE_OPEN), "Capture & Open", 0);

    XFlush(disp);
}

/* the capture mode, format, delay and re-open option are remembered in $XDG_CONFIG_HOME/scr2ppm/gui.conf
 * ("mode=area", "format=png", "delay=5", "reopen=1", "dir=/path/to/folder") */
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
        } else if (strncmp(line, "reopen=", 7) == 0) {
            reopen = (atoi(line + 7) != 0);
        } else if (strncmp(line, "delay=", 6) == 0) {
            int d = atoi(line + 6);
            delay = (d < 0) ? 0 : (d > MAX_DELAY) ? MAX_DELAY : d;
        }
    }
    fclose(f);
}

static void
saveConfig(void){
    char path[600];
    FILE *f;

    if (!configPath(path, sizeof(path), 1) || (f = fopen(path, "w")) == NULL) { return; }
    fprintf(f, "mode=%s\nformat=%s\ndelay=%d\nreopen=%d\ndir=%s\n", mode_arg[mode], fmt_arg[format], delay, reopen, save_dir);
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

/* close the window, give the compositor time to remove it, then run the wrapper;
 * open_after makes the wrapper open the saved screenshot (-o) */
static void
launch(int open_after){
    char dbuf[16];
    char *argv[12];
    int n = 0;

    argv[n++] = "scr2ppm-wrapper";
    argv[n++] = (mode == MODE_SCREEN) ? "-s" : (mode == MODE_WINDOW) ? "-w" : "-a";
    if (delay > 0) {
        snprintf(dbuf, sizeof(dbuf), "%d", delay);
        argv[n++] = "-d";
        argv[n++] = dbuf;
    }
    if (format != FMT_PPM) {
        argv[n++] = "-f";
        argv[n++] = (char *)fmt_arg[format];
    }
    if (save_dir[0]) {
        argv[n++] = "-D";
        argv[n++] = save_dir;
    }
    if (open_after) { argv[n++] = "-o"; }
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
        case HIT_FMT0:
        case HIT_FMT1:
        case HIT_FMT2:    setFormat(id - HIT_FMT0); break;
        case HIT_OPENDIR: openFolder();           break;
        case HIT_CHGDIR:  chooseFolder();         break;
        case HIT_DEFDIR:  resetFolder();          break;
        case HIT_REOPEN:  setReopen(!reopen);    break;
        case HIT_MINUS:   setDelay(delay - 1);    break;
        case HIT_PLUS:    setDelay(delay + 1);    break;
        case HIT_CANCEL:  XCloseDisplay(disp); exit(0);
        case HIT_CAPTURE:      launch(0); break;
        case HIT_CAPTURE_OPEN: launch(1); break;
        default: break;
    }
}

static void
onKey(XKeyEvent *ke){
    KeySym ks = XLookupKeysym(ke, 0);
    switch (ks) {
        case XK_Escape:                       XCloseDisplay(disp); exit(0);
        case XK_Return: case XK_KP_Enter:     launch((ke->state & ShiftMask) != 0); break;
        case XK_s: case XK_1:                 setMode(MODE_SCREEN); break;
        case XK_w: case XK_2:                 setMode(MODE_WINDOW); break;
        case XK_a: case XK_3:                 setMode(MODE_AREA);   break;
        case XK_r:                            setReopen(!reopen); break;
        case XK_o:                            openFolder(); return;
        case XK_c:                            chooseFolder(); break;
        case XK_p:                            setFormat((format + 1) % FMT_COUNT); break;
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

    col_bg     = allocColor("#f6f5f4", WhitePixel(disp, scr));
    col_fg     = allocColor("#2e3436", BlackPixel(disp, scr));
    col_border = allocColor("#b8b6b2", BlackPixel(disp, scr));
    col_accent = allocColor("#3584e4", BlackPixel(disp, scr));
    col_white  = allocColor("#ffffff", WhitePixel(disp, scr));

    int sw = DisplayWidth(disp, scr);
    int sh = DisplayHeight(disp, scr);
    win = XCreateSimpleWindow(disp, RootWindow(disp, scr),
                              (sw - WIN_W) / 2, (sh - WIN_H) / 2, WIN_W, WIN_H,
                              0, col_fg, col_bg);

    XStoreName(disp, win, "Screen 2 PPM");

    XClassHint *ch = XAllocClassHint();
    if (ch) {
        ch->res_name  = "scr2ppm";
        ch->res_class = "scr2ppm";
        XSetClassHint(disp, win, ch);
        XFree(ch);
    }

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
    xdraw = XftDrawCreate(disp, win, DefaultVisual(disp, scr), DefaultColormap(disp, scr));
    allocXftColor(&xc_fg, 0x2e2e, 0x3434, 0x3636);
    allocXftColor(&xc_white, 0xffff, 0xffff, 0xffff);

    XSelectInput(disp, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask);
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
