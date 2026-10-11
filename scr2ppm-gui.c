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

#include<X11/Xlib.h>
#include<X11/Xutil.h>
#include<X11/keysym.h>

#define WIN_W      320
#define WIN_H      250
#define MAX_DELAY  60

enum { MODE_SCREEN, MODE_WINDOW, MODE_AREA, MODE_COUNT };
enum { HIT_NONE, HIT_MODE0, HIT_MODE1, HIT_MODE2, HIT_MINUS, HIT_PLUS, HIT_CANCEL, HIT_CAPTURE, HIT_CAPTURE_OPEN };

typedef struct { int x, y, w, h, id; } Hit;

static const char *mode_label[MODE_COUNT] = {
    "Entire screen",
    "A window (click on it)",
    "An area (drag with the mouse)"
};

/* clickable regions, also used for drawing */
static const Hit hits[] = {
    {  10,  40, 300, 26, HIT_MODE0   },
    {  10,  68, 300, 26, HIT_MODE1   },
    {  10,  96, 300, 26, HIT_MODE2   },
    { 120, 140,  28, 26, HIT_MINUS   },
    { 196, 140,  28, 26, HIT_PLUS    },
    {  10, 200,  70, 30, HIT_CANCEL       },
    {  88, 200,  80, 30, HIT_CAPTURE      },
    { 176, 200, 134, 30, HIT_CAPTURE_OPEN },
};
#define N_HITS ((int)(sizeof(hits) / sizeof(hits[0])))

static Display *disp;
static Window win;
static GC gc;
static XFontStruct *font;

static unsigned long col_bg, col_fg, col_border, col_accent, col_white;

static int mode = MODE_AREA;
static int delay = 0;
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

static XFontStruct *
loadFont(void){
    const char *names[] = {
        "-*-dejavu sans-medium-r-normal--13-*-*-*-*-*-*-*",
        "-*-helvetica-medium-r-normal--12-*-*-*-*-*-*-*",
        "-misc-fixed-medium-r-normal--13-*-*-*-*-*-*-*",
        "fixed",
        NULL
    };
    for (int i = 0; names[i]; i++) {
        XFontStruct *f = XLoadQueryFont(disp, names[i]);
        if (f) { return f; }
    }
    return NULL;
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
drawText(int x, int baseline, const char *s){
    XDrawString(disp, win, gc, x, baseline, s, (int)strlen(s));
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
    unsigned long text = primary ? col_white : col_fg;
    if (is_pressed) { face = primary ? col_fg : col_border; }

    XSetForeground(disp, gc, face);
    XFillRectangle(disp, win, gc, h->x, h->y, h->w, h->h);
    XSetForeground(disp, gc, primary ? col_accent : col_border);
    XDrawRectangle(disp, win, gc, h->x, h->y, h->w - 1, h->h - 1);

    XSetForeground(disp, gc, text);
    int tw = XTextWidth(font, label, (int)strlen(label));
    drawText(h->x + (h->w - tw) / 2, centerBaseline(h->y, h->h), label);
}

static void
draw(void){
    char buf[16];

    XSetForeground(disp, gc, col_bg);
    XFillRectangle(disp, win, gc, 0, 0, WIN_W, WIN_H);

    /* heading */
    XSetForeground(disp, gc, col_fg);
    drawText(20, 28, "What do you want to capture?");

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
        drawText(44, centerBaseline(h->y, h->h), mode_label[i]);
    }

    /* delay row */
    const Hit *minus = findHit(HIT_MINUS);
    const Hit *plus  = findHit(HIT_PLUS);
    XSetForeground(disp, gc, col_fg);
    drawText(20, centerBaseline(minus->y, minus->h), "Delay:");
    drawButton(minus, "-", 0);
    drawButton(plus, "+", 0);

    snprintf(buf, sizeof(buf), delay == 0 ? "none" : "%d s", delay);
    int vx0 = minus->x + minus->w;
    int vw  = plus->x - vx0;
    int tw  = XTextWidth(font, buf, (int)strlen(buf));
    XSetForeground(disp, gc, col_fg);
    drawText(vx0 + (vw - tw) / 2, centerBaseline(minus->y, minus->h), buf);

    /* action buttons */
    drawButton(findHit(HIT_CANCEL), "Cancel", 0);
    drawButton(findHit(HIT_CAPTURE), "Capture", 1);
    drawButton(findHit(HIT_CAPTURE_OPEN), "Capture & Open", 0);

    XFlush(disp);
}

static void
setDelay(int d){
    if (d < 0) { d = 0; }
    if (d > MAX_DELAY) { d = MAX_DELAY; }
    delay = d;
}

/* close the window, give the compositor time to remove it, then run the wrapper;
 * open_after makes the wrapper open the saved screenshot (-o) */
static void
launch(int open_after){
    char dbuf[16];
    char *argv[6];
    int n = 0;

    argv[n++] = "scr2ppm-wrapper";
    argv[n++] = (mode == MODE_SCREEN) ? "-s" : (mode == MODE_WINDOW) ? "-w" : "-a";
    if (delay > 0) {
        snprintf(dbuf, sizeof(dbuf), "%d", delay);
        argv[n++] = "-d";
        argv[n++] = dbuf;
    }
    if (open_after) { argv[n++] = "-o"; }
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

static void
activate(int id){
    switch (id) {
        case HIT_MODE0:
        case HIT_MODE1:
        case HIT_MODE2:   mode = id - HIT_MODE0;  break;
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
        case XK_s: case XK_1:                 mode = MODE_SCREEN; break;
        case XK_w: case XK_2:                 mode = MODE_WINDOW; break;
        case XK_a: case XK_3:                 mode = MODE_AREA;   break;
        case XK_Up: case XK_plus: case XK_equal: case XK_KP_Add:
            setDelay(delay + 1); break;
        case XK_Down: case XK_minus: case XK_KP_Subtract:
            setDelay(delay - 1); break;
        default: return;
    }
    draw();
}

int
main(void){
    disp = XOpenDisplay(NULL);
    if (disp == NULL) {
        fprintf(stderr, "scr2ppm-gui: failed to open display\n");
        return 1;
    }
    int scr = DefaultScreen(disp);

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
    XSetFont(disp, gc, font->fid);

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
