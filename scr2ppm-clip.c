/**
 * description: puts a PNG image (read from stdin) into the X11 clipboard
 *              (works under Wayland too, through XWayland)
 * usage: scr2ppm-clip < image.png
 * The process forks into the background after taking ownership of the clipboard and
 * serves the image to whoever pastes, until another program copies something else.
 * repo: https://github.com/igorlogius/scr2ppm
 **/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>

#include<X11/Xlib.h>
#include<X11/Xatom.h>

#define CHUNK     (128 * 1024)   /* size of the pieces sent for INCR transfers */
#define MAX_XFER  8

typedef struct { Window req; Atom prop; size_t off; int used; } Xfer;

static Display *disp;
static unsigned char *data;
static size_t len;
static Atom a_clip, a_targets, a_png, a_incr;
static size_t max_direct;
static Xfer xfers[MAX_XFER];

static void
sendNotify(XSelectionRequestEvent *rq, Atom prop){
    XSelectionEvent sn;
    memset(&sn, 0, sizeof(sn));
    sn.type = SelectionNotify;
    sn.requestor = rq->requestor;
    sn.selection = rq->selection;
    sn.target = rq->target;
    sn.property = prop;
    sn.time = rq->time;
    XSendEvent(disp, rq->requestor, False, 0, (XEvent *)&sn);
    XFlush(disp);
}

static void
onRequest(XSelectionRequestEvent *rq){
    Atom prop = (rq->property != None) ? rq->property : rq->target;

    if (rq->selection != a_clip) { sendNotify(rq, None); return; }

    if (rq->target == a_targets) {
        Atom list[2] = { a_targets, a_png };
        XChangeProperty(disp, rq->requestor, prop, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)list, 2);
        sendNotify(rq, prop);
    } else if (rq->target == a_png) {
        if (len <= max_direct) {
            XChangeProperty(disp, rq->requestor, prop, a_png, 8, PropModeReplace, data, (int)len);
            sendNotify(rq, prop);
        } else {
            /* too big for one request: INCR transfer, the pieces follow as the requestor deletes the property */
            Xfer *x = NULL;
            long total = (long)len;
            for (int i = 0; i < MAX_XFER; i++) { if (!xfers[i].used) { x = &xfers[i]; break; } }
            if (x == NULL) { sendNotify(rq, None); return; }
            x->used = 1; x->req = rq->requestor; x->prop = prop; x->off = 0;
            XSelectInput(disp, rq->requestor, PropertyChangeMask);
            XChangeProperty(disp, rq->requestor, prop, a_incr, 32, PropModeReplace,
                            (unsigned char *)&total, 1);
            sendNotify(rq, prop);
        }
    } else {
        sendNotify(rq, None);
    }
}

static void
onProperty(XPropertyEvent *pe){
    if (pe->state != PropertyDelete) { return; }
    for (int i = 0; i < MAX_XFER; i++) {
        Xfer *x = &xfers[i];
        if (x->used && x->req == pe->window && x->prop == pe->atom) {
            size_t n = len - x->off;
            if (n > CHUNK) { n = CHUNK; }
            XChangeProperty(disp, x->req, x->prop, a_png, 8, PropModeReplace, data + x->off, (int)n);
            XFlush(disp);
            if (n == 0) {              /* zero length piece ends the transfer */
                XSelectInput(disp, x->req, NoEventMask);
                x->used = 0;
            } else {
                x->off += n;
            }
            return;
        }
    }
}

int
main(void){
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    size_t cap = 1 << 20, n;

    data = malloc(cap);
    if (data == NULL) { return 1; }
    while ((n = fread(data + len, 1, cap - len, stdin)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            data = realloc(data, cap);
            if (data == NULL) { return 1; }
        }
    }
    if (len < 8 || memcmp(data, sig, 8) != 0) {
        fprintf(stderr, "scr2ppm-clip: input is not a PNG image\n");
        return 1;
    }

    disp = XOpenDisplay(NULL);
    if (disp == NULL) {
        fprintf(stderr, "scr2ppm-clip: failed to open display\n");
        return 2;
    }
    a_clip    = XInternAtom(disp, "CLIPBOARD", False);
    a_targets = XInternAtom(disp, "TARGETS", False);
    a_png     = XInternAtom(disp, "image/png", False);
    a_incr    = XInternAtom(disp, "INCR", False);

    long maxreq = XExtendedMaxRequestSize(disp);
    if (maxreq <= 0) { maxreq = XMaxRequestSize(disp); }
    max_direct = (size_t)maxreq * 4 - 1024;

    Window win = XCreateSimpleWindow(disp, DefaultRootWindow(disp), 0, 0, 1, 1, 0, 0, 0);
    XSetSelectionOwner(disp, a_clip, win, CurrentTime);
    if (XGetSelectionOwner(disp, a_clip) != win) {
        fprintf(stderr, "scr2ppm-clip: could not take ownership of the clipboard\n");
        return 2;
    }
    XFlush(disp);

    /* from here on in the background: the caller continues, the image stays available */
    pid_t pid = fork();
    if (pid < 0) { return 1; }
    if (pid > 0) { _exit(0); }
    setsid();
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); }

    for (;;) {
        XEvent ev;
        XNextEvent(disp, &ev);
        switch (ev.type) {
            case SelectionRequest: onRequest(&ev.xselectionrequest); break;
            case PropertyNotify:   onProperty(&ev.xproperty);        break;
            case SelectionClear:   return 0;   /* someone else copied something */
            default: break;
        }
    }
}
