/**
 * desciption: make screenshot from a part of the desktop, outputs a portable pixmap byte stream
 * repo: https://github.com/igorlogius/scr2ppm
 * author: https://github.com/igorlogius
 * refs: https://en.wikipedia.org/wiki/Netpbm#PPM_example
 **/

#include<stdio.h>  /* printf */
#include<stdlib.h> /* atoi */
#include<unistd.h> /* sleep */
#include<string.h> /* popen helpers */
#include<time.h>   /* clock_gettime */

/* X11 includes */
#include<X11/Xlib.h>
#include<X11/X.h>
#include<X11/Xutil.h>
#include<X11/cursorfont.h>

extern int opterr;
extern char* optarg;

typedef struct {
    int w,h,x,y;
} Rect;

void
printRect(Rect *rect){
    fprintf(stderr,"x %d, y %d, w %d, h %d\n", rect->x, rect->y, rect->w, rect->h);
}

void
printXWinAttr(XWindowAttributes *wa){
    fprintf(stderr,"x %d, y %d, w %d, h %d\n", wa->x, wa->y, wa->width, wa->height);
}

void
usage(
	const char* pname,
	const char* msg
){
	fprintf(stderr,"\n");
	fprintf(stderr,"%s\n",msg);
	fprintf(stderr,"\n");
	fprintf(stderr,"(SCR)een (to) (P)ortable (P)ix(M)ap - (SCR2PPM)\n");
	fprintf(stderr,"\n");
	fprintf(stderr,"usage %s -[s|w|a] [-d %%d]\n",pname);
	fprintf(stderr,"\n");
	fprintf(stderr,"  -s := capture the entire screen / desktop\n");
	fprintf(stderr,"  -w := select and capture a window\n");
	fprintf(stderr,"  -a := select and capture a custom area\n");
	fprintf(stderr,"  -d := add a delay in seconds");
	fprintf(stderr,"\n");
	fprintf(stderr,"\n");
	fprintf(stderr,"Examples: \n");
	fprintf(stderr,"	%s -s        # screenshot entire screen / desktop immediately\n",pname);
	fprintf(stderr,"	%s -s -d 10  # screenshot entire screen / desktop after 10 seconds\n",pname);
	fprintf(stderr,"	%s -a        # screenshot a custom area immediately, after selection\n",pname);
	fprintf(stderr,"	%s -w -d 5   # screenshot a window after 5 seconds\n",pname);
	fprintf(stderr,"	%s -a -d 3   # screenshot a custom area after 3 seconds, after selection\n",pname);
	fprintf(stderr,"	%s           # will output this usage message, since -s,-w or -a is required\n",pname);
	fprintf(stderr,"\n");

	// terminate program
	exit(-1);
}

int
getWindowGeometry(
        Display *disp,
        Window *root,
        Window *window,
        Rect *rect
        ) {
    XWindowAttributes window_attributes_return;

    // https://tronche.com/gui/x/xlib/introduction/errors.html#Status
    int status = XGetWindowAttributes(disp, *window, &window_attributes_return);

    if (status != 0) {
        //fprintf(stderr, "border width: %d\n", window_attributes_return.border_width);
        rect->w = window_attributes_return.width;
        rect->h = window_attributes_return.height;
        rect->x = window_attributes_return.x;
        rect->y = window_attributes_return.y;

        if(rect->x < 0){
            printRect(rect);
            fprintf(stderr,"[WARN] selected window partially out of bounds of the display area, the non visible part will not be captured (POOB-X)\n");
            int abs_x = abs(rect->x);
            rect->x = 0;
            rect->w -= (abs_x);
            printRect(rect);
        }

        if(rect->y < 0){
            printRect(rect);
            fprintf(stderr,"[WARN] selected window partially out of bounds of the display area, the non visible part will not be captured (POOB-Y)\n");
            int abs_y = abs(rect->y);
            rect->y = 0;
            rect->h -= (abs_y);
            printRect(rect);
        }

        if(root != NULL){
            XWindowAttributes window_attributes_return_root;
            int root_status = XGetWindowAttributes(disp, *root, &window_attributes_return_root);
            if(root_status != 0) {
                if(rect->x > window_attributes_return_root.width){
                    fprintf(stderr,"[ERRO] selected window is completly out of bounds of the display area and can not be captured (COOB-X)\n");
                    return -1;
                }
                if(rect->y > window_attributes_return_root.height){
                    fprintf(stderr,"[ERRO] selected window is completly out of bounds of the display area and can not be captured (COOB-Y)\n");
                    return -1;
                }

                if((rect->x+rect->w) > window_attributes_return_root.width){
                    printRect(rect);
                    fprintf(stderr,"[WARN] selected window is partially out of bounds of the display area, the non visible part will not be captured (POOB-RIGHT)\n");
                    rect->w -= ((rect->x + rect->w)  - window_attributes_return_root.width);
                    printRect(rect);
                }

                if((rect->y+rect->h) > window_attributes_return_root.height){
                    printRect(rect);
                    fprintf(stderr,"[WARN] selected window is partially out of bounds of the display area, the non visible part will not be captured (POOB-BOTTOM)\n");
                    rect->h -= ((rect->y + rect->h)  - window_attributes_return_root.height);
                    printRect(rect);
                }
            }
        }

        if(rect->w < 1){
            fprintf(stderr,"[ERRO] selected window width to small, nothing to capture (W<1))\n");
            return -1;
        }

        if(rect->h < 1){
            fprintf(stderr,"[ERRO] selected window height to small, nothing to capture (H<1)\n");
            return -1;
        }

        return 0;
    }
    return -1;
}

int
selectWindow(
        Display *disp,
        Window *root,
        Rect *rect
) {
    int rx = 0, ry = 0, rw = 0, rh = 0;
    int rect_x = 0, rect_y = 0, rect_w = 0, rect_h = 0;
    int btn_pressed = 0, done = 0;

    XEvent ev;

    Cursor cursor /*, cursor2*/;
    cursor = XCreateFontCursor(disp, XC_left_ptr);
    //cursor2 = XCreateFontCursor(disp, XC_lr_angle);

    if ((XGrabPointer
                 (disp, *root, False,
                  ButtonPressMask, GrabModeAsync,
                  GrabModeAsync, *root, cursor, CurrentTime) != GrabSuccess)) {
        fprintf(stderr,"couldn't grab pointer:\n");
        return -1;
    }

    while (1) {
        while (XPending(disp)) {
            XNextEvent(disp, &ev);
            switch (ev.type) {
                case ButtonPress:
                    XUngrabPointer(disp, CurrentTime);
                    XFreeCursor(disp, cursor);
                    XFlush(disp);
                    return getWindowGeometry(disp, root, &ev.xbutton.subwindow, rect);
                default:
                    break;
            }
        }
    }
    XUngrabPointer(disp, CurrentTime);
    XFreeCursor(disp, cursor);
    XFlush(disp);
    return -1;
}


int
selectArea(
        Display *disp,
        Window *root,
        Rect *rect
) {
    int rx = 0, ry = 0, rw = 0, rh = 0;
    int rect_x = 0, rect_y = 0, rect_w = 0, rect_h = 0;
    int btn_pressed = 0, done = 0;

    XEvent ev;

    /**/
    Cursor cursor /*, cursor2*/;
    cursor = XCreateFontCursor(disp, XC_left_ptr);
    //cursor2 = XCreateFontCursor(disp, XC_lr_angle);
    /**/

    /**/
    XGCValues gcval;
    gcval.foreground = XWhitePixel(disp, 0);
    gcval.function = GXxor;
    gcval.background = XBlackPixel(disp, 0);
    gcval.plane_mask = gcval.background ^ gcval.foreground;
    gcval.subwindow_mode = IncludeInferiors;

    GC gc;
    gc = XCreateGC(disp, *root,
                   GCFunction | GCForeground | GCBackground | GCSubwindowMode,
                   &gcval);
    /**/

    /* this XGrab* stuff makes XPending true ? */
    if ((XGrabPointer
                 (disp, *root, False,
                  ButtonMotionMask | ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
                  GrabModeAsync, *root, cursor, CurrentTime) != GrabSuccess)) {
        fprintf(stderr,"couldn't grab pointer:");
        return -1;
    }

    while (!done) {
        while (!done && XPending(disp)) {
            XNextEvent(disp, &ev);
            switch (ev.type) {
                case MotionNotify:
                    /* this case is purely for drawing rect on screen */
                    if (btn_pressed) {
                        //if (rect_w) {
                            /* re-draw the last rect to clear it */
                            XDrawRectangle(disp, *root, gc, rect_x, rect_y, rect_w, rect_h);
                        //} else {
                            /* Change the cursor to show we're selecting a region */
                        //    XChangeActivePointerGrab(disp, ButtonMotionMask | ButtonReleaseMask, cursor2, CurrentTime);
                            /**/
                        //}

                        rect_x = rx;
                        rect_y = ry;
                        rect_w = ev.xmotion.x - rect_x;
                        rect_h = ev.xmotion.y - rect_y;

                        if (rect_w < 0) {
                            rect_x += rect_w;
                            rect_w = 0 - rect_w;
                        }
                        if (rect_h < 0) {
                            rect_y += rect_h;
                            rect_h = 0 - rect_h;
                        }

                        XDrawRectangle(disp, *root, gc, rect_x, rect_y, rect_w, rect_h);
                        XFlush(disp);
                    }
                    break;
                case ButtonPress:
                    btn_pressed = 1;
                    rx = ev.xbutton.x;
                    ry = ev.xbutton.y;
                    break;
                case ButtonRelease:
                    done = 1;
                    break;
                default:
                    break;
            } // switch
        } // XPending while
        if(done){
            break;
        }
    } // done while

    /* clear the drawn rectangle */
    if (rect_w) {
        XDrawRectangle(disp, *root, gc, rect_x, rect_y, rect_w, rect_h);
        XFlush(disp);
    }

    /* reset cursor style */
    XChangeActivePointerGrab(disp,
		             ButtonMotionMask | ButtonReleaseMask,
                            cursor, CurrentTime);
    /**/
    XFreeCursor(disp, cursor);
    //XFreeCursor(disp, cursor2);
    XUngrabPointer(disp, CurrentTime);
    XFreeGC(disp, gc);
    XSync(disp,True);

    rw = ev.xbutton.x - rx;
    rh = ev.xbutton.y - ry;

    if (rw < 0) {
        rx += rw;
        rw = 0 - rw;
    }
    if (rh < 0) {
        ry += rh;
        rh = 0 - rh;
    }

    rect->w = rw;
    rect->h = rh;
    rect->x = rx;
    rect->y = ry;


    return 0;
}

/* use slop for the interactive area selection; returns 0 on success */
int
selectAreaSlop(
        Display *disp,
        Window *root,
        Rect *rect
) {
    int x, y, w, h;
    XWindowAttributes ra;

    /* -t 0 disables window-snapping, so click+drag is always an area.
       slop's own stderr is left visible so GL/X errors show up. */
    FILE *fp = popen("slop -t 0 -f '%x %y %w %h'", "r");
    if (fp == NULL) {
        fprintf(stderr, "[ERRO] failed to start slop\n");
        return -1;
    }

    int n = fscanf(fp, "%d %d %d %d", &x, &y, &w, &h);
    int status = pclose(fp);

    if (n != 4 || status != 0) {
        fprintf(stderr, "[ERRO] selection cancelled or slop failed\n");
        return -1;
    }

    /* clamp to the root window */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (XGetWindowAttributes(disp, *root, &ra) != 0) {
        if (x + w > ra.width)  w = ra.width  - x;
        if (y + h > ra.height) h = ra.height - y;
    }
    if (w < 1 || h < 1) {
        fprintf(stderr, "[ERRO] empty selection\n");
        return -1;
    }

    rect->x = x; rect->y = y; rect->w = w; rect->h = h;

    /* give the compositor a moment to remove slop's overlay */
    XSync(disp, False);
    usleep(150000);
    return 0;
}

#define BORDER_PX 4

static double
nowSeconds(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static unsigned long
allocColor(Display *disp, const char *name, unsigned long fallback){
    int scr = DefaultScreen(disp);
    XColor color, exact;
    if (XAllocNamedColor(disp, DefaultColormap(disp, scr), name, &color, &exact)) {
        return color.pixel;
    }
    return fallback;
}

/* Outer box of the border around rect. Normally just OUTSIDE rect; if rect
 * touches a screen edge (full screen, maximized window, ...) there is no room
 * outside, so the border is placed INSIDE rect instead. */
static void
borderBox(Display *disp, Rect *rect, int *Ox, int *Oy, int *Ow, int *Oh){
    const int t = BORDER_PX;
    int scr = DefaultScreen(disp);
    int sw = DisplayWidth(disp, scr);
    int sh = DisplayHeight(disp, scr);
    int inside = (rect->x < t) || (rect->y < t) ||
                 (rect->x + rect->w > sw - t) || (rect->y + rect->h > sh - t);
    *Ox = inside ? rect->x : rect->x - t;
    *Oy = inside ? rect->y : rect->y - t;
    *Ow = inside ? rect->w : rect->w + 2*t;
    *Oh = inside ? rect->h : rect->h + 2*t;
}

/* Shows a border around rect for `seconds`. It starts completely red and the
 * red drains clockwise from the top-left, revealing blue underneath, so the
 * border stays fully visible and ends up completely blue. It is removed again
 * afterwards.
 *
 * Normally the border is drawn just OUTSIDE rect. If rect touches a screen edge
 * (full screen, maximized window, ...) there is no room outside, so the border
 * is drawn INSIDE rect instead. Either way it is gone (and the compositor has
 * had time to notice) before the capture, so it never ends up in the image. */
void
delayWithBorder(
        Display *disp,
        Window root,
        Rect *rect,
        int seconds
) {
    const int t = BORDER_PX;
    int scr = DefaultScreen(disp);
    unsigned long red  = allocColor(disp, "#ff3b30", WhitePixel(disp, scr));
    unsigned long blue = allocColor(disp, "#0a84ff", BlackPixel(disp, scr));

    int Ox, Oy, Ow, Oh;   /* outer box of the border */
    borderBox(disp, rect, &Ox, &Oy, &Ow, &Oh);

    /* four sides on a clockwise path: 0 top, 1 right, 2 bottom, 3 left */
    int side = Oh - 2*t; if (side < 0) side = 0;
    int len[4] = { Ow, side, Ow, side };
    int start[4];
    int total = 0;
    for (int i = 0; i < 4; i++) { start[i] = total; total += len[i]; }

    /* full-length geometry of each side */
    int bx[4] = { Ox,           Ox + Ow - t,  Ox,           Ox };
    int by[4] = { Oy,           Oy + t,       Oy + Oh - t,  Oy + t };
    int bw[4] = { Ow,           t,            Ow,           t };
    int bh[4] = { t,            side,         t,            side };

    Window under[4], over[4];
    XSetWindowAttributes swa;
    swa.override_redirect = True;

    /* blue bars: static, full length, underneath */
    swa.background_pixel = blue;
    for (int i = 0; i < 4; i++) {
        under[i] = XCreateWindow(disp, root, bx[i], by[i],
                                 bw[i] > 0 ? bw[i] : 1, bh[i] > 0 ? bh[i] : 1, 0,
                                 CopyFromParent, InputOutput, CopyFromParent,
                                 CWOverrideRedirect | CWBackPixel, &swa);
        if (len[i] > 0) { XMapWindow(disp, under[i]); }
    }
    /* red bars: created and mapped after the blue ones, so they stack on top */
    swa.background_pixel = red;
    for (int i = 0; i < 4; i++) {
        over[i] = XCreateWindow(disp, root, bx[i], by[i],
                                bw[i] > 0 ? bw[i] : 1, bh[i] > 0 ? bh[i] : 1, 0,
                                CopyFromParent, InputOutput, CopyFromParent,
                                CWOverrideRedirect | CWBackPixel, &swa);
        if (len[i] > 0) { XMapWindow(disp, over[i]); }
    }
    XFlush(disp);

    double t0 = nowSeconds();
    double elapsed;
    while ((elapsed = nowSeconds() - t0) < seconds) {
        int pos = (int)(elapsed / seconds * total);   /* red remains on [pos, total) */

        for (int i = 0; i < 4; i++) {
            int a = pos > start[i] ? pos : start[i];
            int b = start[i] + len[i];
            if (b - a < 1) { XUnmapWindow(disp, over[i]); continue; }
            int from = a - start[i];
            int to   = b - start[i];
            int n    = to - from;
            switch (i) {
                case 0: XMoveResizeWindow(disp, over[i], Ox + from, Oy, n, t); break;
                case 1: XMoveResizeWindow(disp, over[i], Ox + Ow - t, Oy + t + from, t, n); break;
                case 2: XMoveResizeWindow(disp, over[i], Ox + (len[i] - to), Oy + Oh - t, n, t); break;
                case 3: XMoveResizeWindow(disp, over[i], Ox, Oy + t + (len[i] - to), t, n); break;
            }
            XMapWindow(disp, over[i]);
        }
        XFlush(disp);
        usleep(30000);   /* ~30 fps */
    }

    for (int i = 0; i < 4; i++) { XDestroyWindow(disp, over[i]); XDestroyWindow(disp, under[i]); }
    XSync(disp, False);
    usleep(150000);      /* let the compositor drop the bars before capturing */
}

#define FLASH_MS 250

/* Brief green flash of the border around the captured area, as feedback that
 * the capture happened. Called AFTER the image was grabbed, so it never ends
 * up in the screenshot. */
void
flashBorder(
        Display *disp,
        Window root,
        Rect *rect
) {
    const int t = BORDER_PX;
    unsigned long green = allocColor(disp, "#34c759", WhitePixel(disp, DefaultScreen(disp)));

    int Ox, Oy, Ow, Oh;
    borderBox(disp, rect, &Ox, &Oy, &Ow, &Oh);
    int side = Oh - 2*t; if (side < 0) side = 0;

    int bx[4] = { Ox,  Ox + Ow - t,  Ox,           Ox };
    int by[4] = { Oy,  Oy + t,       Oy + Oh - t,  Oy + t };
    int bw[4] = { Ow,  t,            Ow,           t };
    int bh[4] = { t,   side,         t,            side };

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.background_pixel = green;

    Window bar[4];
    for (int i = 0; i < 4; i++) {
        bar[i] = XCreateWindow(disp, root, bx[i], by[i],
                               bw[i] > 0 ? bw[i] : 1, bh[i] > 0 ? bh[i] : 1, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWOverrideRedirect | CWBackPixel, &swa);
        if (bw[i] > 0 && bh[i] > 0) { XMapWindow(disp, bar[i]); }
    }
    XFlush(disp);
    usleep(FLASH_MS * 1000);

    for (int i = 0; i < 4; i++) { XDestroyWindow(disp, bar[i]); }
    XSync(disp, False);
}

int
main(
    int argc,
    char *argv[]
    ) {
    int ret = -1;   // exit code
    int delay = -1; //
    int mode = -1;  //
    int opt = -1;   //
    opterr = 0;     // supress invalid option -- 'xyz' output
    int x = -1;
    int y = -1;
    unsigned long pixel = -1;
    Rect rect;
    XImage *image = NULL;

    // get options
    while((opt = getopt(argc, argv, "swad:")) != -1)
    {
        switch (opt)
        {
            case 's': mode = 0;break;
            case 'w': mode = 1;break;
            case 'a': mode = 2;break;
            case 'd': delay = atoi(optarg);break;
            default: usage(argv[0],"");break;
        }
    }

    // get display + root window
    Display *disp = XOpenDisplay(NULL);
    if(disp == NULL) { usage(argv[0], "ERR: failed to open display"); }

    Window root = XDefaultRootWindow(disp);

    switch (mode)
    {
        case 0: ret = getWindowGeometry(disp, NULL, &root, &rect); break;
        case 1: ret = selectWindow(disp, &root, &rect);       break;
        case 2: ret = selectAreaSlop(disp, &root, &rect);     break;
        default: usage(argv[0],"");break;
    }


    // error getting selection
    if( ret != 0) {
        XDestroyWindow(disp, root);
        XCloseDisplay(disp);
        return -1;
    }

    // get Image Data
    //printRect(&rect);
    if(delay > 0) {
        delayWithBorder(disp, root, &rect, delay);   /* red border drains to blue */
    }
    image = XGetImage(disp, root, rect.x, rect.y, rect.w, rect.h, AllPlanes, ZPixmap);
    XDestroyWindow(disp, root);

    if (image == NULL) {
        fprintf(stderr,"ERROR: failed to get image data");
        return -1;
    }

    flashBorder(disp, root, &rect);   /* visual "it happened" cue */
    XBell(disp,0);
    XCloseDisplay(disp);

    // write PPM P6 (binary) header
    fprintf(stdout, "P6\n%d %d\n255\n", rect.w, rect.h);

    for (y = 0; y < rect.h; y++)
    {
        for (x = 0; x < rect.w; x++)
        {
            pixel = XGetPixel(image, x, y);

            fputc((pixel & image->red_mask) >> 16, stdout);
            fputc((pixel & image->green_mask) >> 8, stdout);
            fputc((pixel & image->blue_mask), stdout);
        }
    }

    // release image memory
    XDestroyImage(image);
    return 0;
}

