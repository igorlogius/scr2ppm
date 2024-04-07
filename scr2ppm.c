/**
 * desciption: make screenshot from parts of your desktop as portable pixmap data
 * repo: https://github.com/igorlogius/scr2ppm
 * author: https://github.com/igorlogius
 * refs: https://en.wikipedia.org/wiki/Netpbm#PPM_example
 **/

#include<stdio.h>  /* printf */
#include<stdlib.h> /* atoi */
#include<unistd.h> /* sleep */

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

int
main(
    int argc,
    char *argv[]
    ) {
    int ret = -1;   // exit code
    int delay = -1; //
    int mode = -1;  //
    int opt = -1;   //
    opterr = 0; // supress invalid option -- 'xyz' output
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
        case 2: ret = selectArea(disp, &root, &rect);         break;
        default: usage(argv[0],"");break;
    }


    // error getting selection
    if( ret != 0) {
        XDestroyWindow(disp, root);
        XCloseDisplay(disp);
        return -1;
    }

    /**
     * TODO:
     * - add a countdown
     * - add / keep the the border
     * */

    // get Image Data
    //printRect(&rect);
    image = XGetImage(disp, root, rect.x, rect.y, rect.w, rect.h, AllPlanes, ZPixmap);
    XDestroyWindow(disp, root);

    if (image == NULL) {
        fprintf(stderr,"ERROR: failed to get image data");
        return -1;
    }

    if(delay > 0) {sleep(delay);}

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

