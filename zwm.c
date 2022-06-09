#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>

#include "log.h"

#define length(_x) ((int)(sizeof(_x) / sizeof(_x[0])))

struct client {
    Window window;
    int workspace, screen;
    struct client *next, *prev;
};

#define nworkspace 10
struct client **clients[nworkspace];
Display *display;
Window root, empty;
int running, last_workspace, workspace,
    nscreen;
Cursor cursor;
XineramaScreenInfo *screen_info;
Atom wm_protocols, wm_delete;

struct {
    int x, y, screen;
} workspace_info[nworkspace];

void spawn(void*);
void execsh(void*);
void workspace_switch_to(void*);
void workspace_back(void);
void quit(void);
void client_exit(void);
void client_next(void);
void move_pointer(void);
#define MOD(_mod) Mod1Mask|_mod
#define SPAWN(key, arg)        {MOD(0), key, (void*)(spawn), arg}
#define EXECSH(key, arg)       {MOD(0), key, (void*)(execsh), arg}
#define WORKSPACE(a)           {MOD(0), XK_##a, workspace_switch_to, #a}
#define KEY(mod, key, func)    {MOD(mod), key, (void*)(func), NULL}
struct {
    unsigned int mod;
    KeySym key;
    void *func, *arg;
} keys[] = {
    SPAWN(XK_Return,  "zt"       ),
    SPAWN(XK_b,       "chromium" ),
    EXECSH(XK_s,      "scrot -s -q 100 -o ~/snapshot.png" ),
    KEY(ShiftMask,  XK_c,       client_exit     ),
    KEY(ShiftMask,  XK_q,       quit            ),
    KEY(0,          XK_Tab,     workspace_back  ),
    KEY(0,          XK_n,       client_next     ),
    KEY(0,          XK_m,       move_pointer    ),
    WORKSPACE(1),
    WORKSPACE(2),
    WORKSPACE(3),
    WORKSPACE(4),
    WORKSPACE(5),
    WORKSPACE(6),
    WORKSPACE(7),
    WORKSPACE(8),
    WORKSPACE(9),
    WORKSPACE(0)
};

#define wid(w)       ((long)(w))
#define cid(c)       wid(c->window)
#define cur_screen   workspace_info[workspace].screen
#define cur_px       workspace_info[workspace].x
#define cur_py       workspace_info[workspace].y
#define cur_sx       screen_info[cur_screen].x_org
#define cur_sy       screen_info[cur_screen].y_org
#define cur_sw       screen_info[cur_screen].width
#define cur_sh       screen_info[cur_screen].height
#define cur_sox      (cur_sx + cur_sw / 2)
#define cur_soy      (cur_sy + cur_sh / 2)
#define cur_client   clients[workspace][cur_screen]
#define cur_window   clients[workspace][cur_screen]->window
#define head(c)      clients[c->workspace][c->screen]

void
set_pointer(int x, int y) {
    XWarpPointer(display, None, root, 0, 0, 0, 0, x, y);
    XSync(display, False);
}

void
get_pointer(void) {
    int di;
    unsigned int dui;
    Window dw;
    XQueryPointer(display, root, &dw, &dw, &cur_px, &cur_py, &di, &di, &dui);
}

void
client_info(void) {
    int i, j, has_clients;
    struct client *c;

#define fmt0 "workspace%d"
#define fmt1 "|--screen%d"
#define fmt2 "   |--%ld"
    if (cur_client != NULL)
        log("current: %ld %d %d", wid(cur_window),
            workspace, cur_screen );
    for (i=0; i<nworkspace; i++) {
        has_clients = 0;
        for (j=0; j<nscreen; j++ )
            if (clients[i][j] != NULL) {
                has_clients = 1;
                break;
            }
        if (!has_clients)
            continue;
        log(fmt0, i);
        for (j=0; j<nscreen; j++) {
            c = clients[i][j];
            if (c == NULL)
                continue;
            log(fmt1, j);
            log(fmt2, cid(c));
            while(c->next != head(c)) {
                c = c->next;
                log(fmt2, cid(c));
            }
        }
    }
#undef fmt0
#undef fmt1
#undef fmt2
}

void
delete_client(struct client *c) {
    if (c == NULL)
        return;
    log("%ld", cid(c));
    if (c->next == c) {
        head(c) = NULL;
    } else {
        c->next->prev = c->prev;
        c->prev->next = c->next;
        if (head(c) == c)
            head(c) = c->prev;
    }
    free(c);
    client_info();
}

struct client *
new_client(Window w) {
    struct client *c =
        (struct client*) malloc(sizeof(struct client));
    c->window = w;
    c->screen = cur_screen;
    c->workspace = workspace;
    log("%ld", cid(c));
    if (cur_client == NULL) {
        c->next = c;
        c->prev = c;
    } else {
        c->next = cur_client->next;
        c->prev = cur_client;
        cur_client->next->prev = c;
        cur_client->next = c;
    }
    cur_client = c;
    client_info();
    return c;
}

struct client *
find_client(Window w) {
    struct client *c;
    int i, j;

    for (i=0; i<nworkspace; i++)
        for (j=0; j<nscreen; j++ ) {
            c = clients[i][j];
            if (c == NULL)
                continue;
            if (c->window == w)
                return c;
            while(c->next != head(c)) {
                c = c->next;
                if (c->window == w)
                    return c;
            }
        }
    return NULL;
}

void
focus(void) {
    static struct client *last_client=NULL;
    if (cur_client == NULL)
        return;
    if (cur_client == last_client)
        return;
    last_client = cur_client; // avoid loop focus
    XRaiseWindow(display, cur_window);
    XSetInputFocus(display, cur_window,
        RevertToPointerRoot, CurrentTime);
    XSync(display, False);
}

void
client_exit(void) {
    int n, exists=0;
    Atom *protocols;
    XEvent ev;

    if (cur_client == NULL)
        return;
    log("%ld", cid(cur_client));
    if (XGetWMProtocols(display, cur_window, &protocols, &n)) {
        while (!exists && n--)
            exists = protocols[n] == wm_delete;
        XFree(protocols);
    }
    if (exists) {
        ev.type = ClientMessage;
        ev.xclient.window = cur_window;
        ev.xclient.message_type = wm_protocols;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = wm_delete;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(display, cur_window, False, NoEventMask, &ev);
        XSync(display, True);
        return;
    }

    log("[force] %ld", cid(cur_client));
    XGrabServer(display);
    XDestroyWindow(display, cur_window);
    delete_client(cur_client);
    XSync(display, False);
    XUngrabServer(display);
    focus();
}

void
client_next(void) {
    if (cur_client == NULL)
        return;
    cur_client = cur_client->next;
    focus();
}

void
move_pointer(void) {
    if (nscreen == 1)
        return;
    cur_screen = (cur_screen + 1) % nscreen;
    log("%d", cur_screen);
    set_pointer(cur_sox, cur_soy);
    focus();
}

void
workspace_switch_to(void *arg) {
    int w;

    w = ((char*)arg)[0] - '0';
    if (w == workspace)
        return;

    get_pointer();
    last_workspace = workspace;
    workspace = w;
    log("workspace%d", workspace);

    XRaiseWindow(display, empty);
    XSync(display, False);
    for(int i=0; i<nscreen; i++)
        if (clients[workspace][i] != NULL)
            XRaiseWindow(display, clients[workspace][i]->window);
    set_pointer(cur_px, cur_py);
    focus();
}

void
workspace_back(void) {
    char a = last_workspace + '0';
    workspace_switch_to(&a);
}

void
_EnterNotify(XEvent *e) {
    struct client *c;
    Window w;

    w = ((XCrossingEvent*)&e->xcrossing)->window;
    log("%ld", wid(w));
    if ((c=find_client(w)) == NULL)
        return;
    workspace = c->workspace;
    cur_screen = c->screen;
    focus();
}

void
_MapRequest(XEvent *e) {
    struct client *c;
    Window w;

    w = e->xmaprequest.window;
    log("%ld", wid(w));
    if ((c=find_client(w)) == NULL)
        c = new_client(w);

    XMoveResizeWindow(display, c->window,
        screen_info[c->screen].x_org,
        screen_info[c->screen].y_org,
        screen_info[c->screen].width,
        screen_info[c->screen].height );
    XSelectInput(display, c->window, EnterWindowMask);
    XMapWindow(display, c->window);
    focus();
}

void
_DestroyNotify(XEvent *e) {
    Window w;
    struct client *c;

    w = e->xdestroywindow.window;
    log("%ld", wid(w));
    if ((c=find_client(w)) == NULL)
        return;

    delete_client(c);
    focus();
}

void
execsh(void *arg) {
    char *cmd = (char*)arg;

    log("%s", cmd);
    if (fork() == 0) {
        if (display)
            close(ConnectionNumber(display));
        system(cmd);
        exit(EXIT_SUCCESS);
    }
}

void
spawn(void *arg) {
    char *cmd = (char*)arg;
    char *a[2];

    log("%s", cmd);
    if (fork() == 0) {
        if (display)
            close(ConnectionNumber(display));
        setsid();
        a[0] = cmd;
        a[1] = NULL;
        execvp(cmd, a);
        exit(EXIT_SUCCESS);
    }
}

void
_KeyPress(XEvent *e) {
    for(int i=0; i<length(keys); i++)
        if (keys[i].key
            == XKeycodeToKeysym(display, (KeyCode)e->xkey.keycode, 0)
            && (e->xkey.state == keys[i].mod)) {
            if (keys[i].arg == NULL)
                ((void(*)())(keys[i].func))();
            else
                ((void(*)(void*))(keys[i].func))(keys[i].arg);
        }
}

int
xerror(Display *display, XErrorEvent *e) {
    XSync(display, False);
    log("error: %d", e->error_code);
    return 0;
}

void
setup(void) {
    XSetWindowAttributes wa;
    KeyCode code;
    int i, j, s, w, h;

    log();
    if (!(display = XOpenDisplay(NULL)))
        die("cannot open display");

    XSetErrorHandler(xerror);
    s = DefaultScreen(display);
    w = DisplayWidth(display, s);
    h = DisplayHeight(display, s);
    root = RootWindow(display, s);

    wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    wm_delete    = XInternAtom(display, "WM_DELETE_WINDOW", False);

    wa.cursor = cursor = XCreateFontCursor(display, XC_left_ptr);
    wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask;
    XChangeWindowAttributes(display, root, CWEventMask|CWCursor, &wa);
    XSelectInput(display, root, wa.event_mask);

    XUngrabKey(display, AnyKey, AnyModifier, root);
    for (i = 0; i < length(keys); i++)
        if ((code = XKeysymToKeycode(display, keys[i].key)))
                XGrabKey(display, code, keys[i].mod, root, True,
        GrabModeAsync, GrabModeAsync);
    XSync(display, False);
    empty = XCreateSimpleWindow(display, root, 0, 0, w, h, 0,
        WhitePixel(display, root), BlackPixel(display, root));
    log("root: %ld", wid(root));
    log("empty: %ld", wid(empty));
    XMapRaised(display, empty);

    if (!XineramaIsActive(display)) {
        log("Xinerama is not Active");
        nscreen = 1;
        screen_info = malloc(sizeof(XineramaScreenInfo));
        screen_info[0].x_org = 0;
        screen_info[0].y_org = 0;
        screen_info[0].width = w;
        screen_info[0].height = h;
    } else {
        screen_info = XineramaQueryScreens(display, &nscreen);
    }

    for(i=0; i<nscreen; i++) {
        log("screen%i: pos: (%4d,%4d), size: %dx%d",
                i, screen_info[i].x_org, screen_info[i].y_org,
                screen_info[i].width, screen_info[i].height);
    }

    for(i=0; i<nworkspace; i++) {
        clients[i] = (struct client**) malloc(nscreen * sizeof(struct client*));
        for(j=0; j<nscreen; j++)
            clients[i][j] = NULL;
        workspace = i;
        cur_screen = 0;
        cur_px = cur_sox;
        cur_py = cur_soy;
    }
    last_workspace = workspace = 1;
    cur_screen = 0;
    set_pointer(cur_px, cur_py);
}

void
run(void) {
    XEvent e;

    log();
    XSync(display, False);
    running = 1;

#define H(type) \
    case type: \
        _##type(&e); \
        break;

    while(running && !XNextEvent(display, &e)) {
        switch(e.type) {
            H(KeyPress)
            H(MapRequest)
            H(EnterNotify)
            H(DestroyNotify)
            case UnmapNotify:
            case CreateNotify:
            case MapNotify:
            case MappingNotify:
            case ConfigureNotify:
            case ConfigureRequest:
            case KeyRelease:
            case ClientMessage:
                break;
            default:
                log("Unsupport event %d", e.type);
        }
    }

#undef H
}

void
quit(void) {
    running = 0;
}

void
clean(void) {
    int i, j;

    XGrabServer(display);
    log();
    for (i=0; i<nworkspace; i++) {
        log("workspace%d", i);
        workspace = i;
        for (j=0; j<nscreen; j++ ) {
            cur_screen = j;
            while(cur_client != NULL) {
                log("screen%d, window: %ld",
                    cur_screen, wid(cur_window));
                XDestroyWindow(display, cur_window);
                delete_client(cur_client);
                XSync(display, False);
            }
        }
    }
    client_info();
    for(i=0; i<nworkspace; i++)
        free(clients[i]);
    if (!XineramaIsActive(display))
        free(screen_info);
    else
        XFree(screen_info);
    XDestroyWindow(display, empty);
    XFreeCursor(display, cursor);
    XUngrabKey(display, AnyKey, AnyModifier, root);
    XSync(display, False);
    XUngrabServer(display);
    XCloseDisplay(display);
}

int
main(void) {
    if(log_open("zwm") != 0)
	 die("can't open log");

    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
        die("can't set SIGCHLD");

    setup();
    run();
    clean();

    log_close();
    return 0;
}
