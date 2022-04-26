#include "stdio.h"
#include "signal.h"
#include "stdlib.h"
#include "unistd.h"
#include "string.h"
#include "time.h"

#include "X11/Xlib.h"
#include "X11/Xatom.h"
#include "X11/Xutil.h"
#include "X11/cursorfont.h"
#include "X11/extensions/Xinerama.h"

#define _log(_fmt, ...) {\
    time_t t = time(NULL); \
    struct tm *lt = localtime(&t); \
    fprintf(log_fd, \
        "[%02d:%02d:%02d %02d/%02d/%4d] [%s] "_fmt"\n",\
        lt->tm_hour, lt->tm_min, lt->tm_sec, \
        lt->tm_mon+1, lt->tm_mday, lt->tm_year+1900, \
        __FUNCTION__, ##__VA_ARGS__); \
    fflush(log_fd); \
}

#define die(_fmt, ...) { \
    _log("die "_fmt"\n", ##__VA_ARGS__ ); \
    exit(1); \
} 

#define warn(_fmt, ...) \
    _log("WARNING "_fmt, ##__VA_ARGS__ )

#define DEBUG
#ifdef DEBUG
#define debug(_fmt, ...) _log(_fmt, ##__VA_ARGS__) 
#else
#define debug(_fmt, ...) {}
#endif

#define length(_x) ((int)(sizeof(_x) / sizeof(_x[0])))

struct client {
    Window window;
    int workspace, screen;
    struct client *next, *prev;
};

#define nworkspace 10
struct client **clients[nworkspace];   
struct client *last_client=NULL;
Display *display;
Window root, empty;
int running, workspace_last, workspace, screen,
    nscreen;
Cursor cursor;
XineramaScreenInfo *screens;
FILE *log_fd;
Atom wm_protocols, wm_delete;

void spawn(void*);
void workspace_switch_to(void*);
void workspace_back();
void quit();
void client_exit();
void client_next();
void move_pointer();
#define MOD(_mod) Mod1Mask|_mod
#define SPAWN(key, arg)        {MOD(0), key, (void*)(spawn), arg}
#define WORKSPACE(a)           {MOD(0), XK_##a, workspace_switch_to, #a}
#define KEY(mod, key, func)    {MOD(mod), key, (void*)(func), NULL}
struct {
    unsigned int mod;
    KeySym key;
    void *func, *arg;
} keys[] = {
    SPAWN(XK_Return,  "st"       ),
    SPAWN(XK_b,       "chromium" ),
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

void key(XEvent*);
void map_request(XEvent*);
void destory_notify(XEvent*);
void configure_request(XEvent*);
void configure_notify(XEvent*);
void enter_notify(XEvent*);
struct {
    int type;
    void (*func)(XEvent *);
} handlers[] = {
    {KeyPress,         key              },
    {MapRequest,       map_request      },
    {EnterNotify,      enter_notify     },
    {DestroyNotify,    destory_notify   }
};

#define wid(w)      ((long)(w))
#define cid(c)      wid(c->window)
#define cur_client  clients[workspace][screen]
#define cur_window  clients[workspace][screen]->window
#define head(c)     clients[c->workspace][c->screen]

void
set_screen() {
    int di, x, y, s;
    unsigned int dui;

    Window dw;
    XQueryPointer(display, root, &dw, &dw, &x, &y, &di, &di, &dui);
    for(s=0; s<nscreen; s++)
        if (x >= screens[s].x_org && 
            x <= screens[s].x_org+screens[s].width &&
            y >= screens[s].y_org &&
            y <= screens[s].y_org+screens[s].height)
            break;
    screen = s;
}

void
client_info() {
#ifdef DEBUG
    int i, j, has_clients;
    struct client *c;

#define fmt0 "workspace%d"
#define fmt1 "|--screen%d"
#define fmt2 "   |--%ld"
    if (cur_client != NULL)
        debug("current: %ld %d %d", wid(cur_window),
            workspace, screen );
    for (i=0; i<nworkspace; i++) {
        has_clients = 0;
        for (j=0; j<nscreen; j++ )
            if (clients[i][j] != NULL) {
                has_clients = 1;
                break;
            }
        if (!has_clients)
            continue;
        debug(fmt0, i);
        for (j=0; j<nscreen; j++) {
            c = clients[i][j];
            if (c == NULL)
                continue;
            debug(fmt1, j);
            debug(fmt2, cid(c));
            while(c->next != head(c)) {
                c = c->next;
                debug(fmt2, cid(c));
            }
        }
    }
#undef fmt0
#undef fmt1
#undef fmt2
#endif
}

void
delete_client(struct client *c) {
    if (c == NULL)
        return;
    debug("%ld", cid(c));
    if (c->next == c) {
        head(c) = NULL;
    } else {
        c->next->prev = c->prev;
        c->prev->next = c->next;
        if (head(c) == c)
            head(c) = c->next;
    }
    free(c);
    client_info();
}

struct client *
new_client(Window w) {
    struct client *c =
        (struct client*) malloc(sizeof(struct client));
    set_screen();
    c->window = w;
    c->screen = screen;
    c->workspace = workspace;
    debug("%ld", cid(c));
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
focus() {
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
client_exit() {
    int n, exists=0;
    Atom *protocols;
    XEvent ev;

    if (cur_client == NULL)
        return;
    debug("%ld", cid(cur_client));
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

    debug("[force] %ld", cid(cur_client));
    XGrabServer(display);
    XDestroyWindow(display, cur_window);
    delete_client(cur_client);
    XSync(display, False);
    XUngrabServer(display);
    focus();
}

void
client_next() {
    if (cur_client == NULL)
        return;
    cur_client = cur_client->next;
    focus();
}

void
move_pointer() {
    if (nscreen == 1)
        return;
    screen = (screen + 1) % nscreen;
    debug("%d", screen);
    XWarpPointer(display, None, root, 0, 0, 0, 0, 
        screens[screen].x_org + screens[screen].width/2,
        screens[screen].y_org + screens[screen].height/2
    );
    focus();
}

void
workspace_switch_to(void *arg) {
    workspace_last = workspace;
    workspace = ((char*)arg)[0] - '0';
    debug("workspace%d", workspace);
    if (workspace_last == workspace)
        return;
    XRaiseWindow(display, empty);
    XSync(display, False);
    for(int i=0; i<nscreen; i++)
        if (clients[workspace][i] != NULL)
            XRaiseWindow(display, clients[workspace][i]->window);
    focus();
}

void
workspace_back() {
    char a = workspace_last + '0';
    workspace_switch_to(&a);
}

void
enter_notify(XEvent *e) {
    struct client *c;
    Window w;

    w = ((XCrossingEvent*)&e->xcrossing)->window;
    debug("%ld", wid(w));
    if ((c=find_client(w)) == NULL)
        return;
    workspace = c->workspace;
    screen = c->screen;
    focus();
}

void
map_request(XEvent *e) {
    struct client *c;
    Window w;

    w = e->xmaprequest.window;
    debug("%ld", wid(w));
    if ((c=find_client(w)) == NULL)
        c = new_client(w);
    
    XMoveResizeWindow(display, c->window, 
        screens[c->screen].x_org,
        screens[c->screen].y_org,
        screens[c->screen].width,
        screens[c->screen].height );
    XSelectInput(display, c->window, EnterWindowMask);
    XMapWindow(display, c->window);
    focus();
}

void
destory_notify(XEvent *e) {
    Window w;
    struct client *c;

    w = e->xdestroywindow.window;
    debug("%ld", wid(w));
    if ((c=find_client(w)) == NULL)
        return;

    delete_client(c);
    focus();
}

void
spawn(void *arg) {
    char *cmd = (char*)arg;
    char *a[] = { NULL };

    debug("%s", cmd);
    if (fork() == 0) {
        if (display) 
            close(ConnectionNumber(display));
        setsid();
        execvp(cmd, a);
        exit(EXIT_SUCCESS);
    }
}

void
key(XEvent *e) {
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
    warn("error: %d", e->error_code);
    return 0;
}

void
setup() {
    XSetWindowAttributes wa;
    KeyCode code;
    int i, j, s, w, h;

    debug();
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
    debug("root: %ld", wid(root));
    debug("empty: %ld", wid(empty));
    XMapRaised(display, empty);

    if (!XineramaIsActive(display)) {
        warn("Xinerama is not Active");
        nscreen = 1;
        screens = malloc(sizeof(XineramaScreenInfo));
        screens[0].x_org = 0;
        screens[0].y_org = 0;
        screens[0].width = w;
        screens[0].height = h;
    } else {
        screens = XineramaQueryScreens(display, &nscreen);
    }

    workspace_last = workspace = 1;
    screen = 1;
    for(i=0; i<nworkspace; i++) {
        clients[i] = (struct client**) malloc(nscreen * sizeof(struct client*));
        for(j=0; j<nscreen; j++)
            clients[i][j] = NULL;
    }
    for(i=0; i<nscreen; i++) {
        debug("screen%i: pos: (%4d,%4d), size: %dx%d", 
                i, screens[i].x_org, screens[i].y_org,
                screens[i].width, screens[i].height);
    }
}

void 
run() {
    XEvent e;
    int i, n;

    XSync(display, False);
    running = 1;
    debug();
    while(running && !XNextEvent(display, &e)) {
        n = length(handlers);
        for(i=0; i<n; i++) {
            if (e.type == handlers[i].type) {
                if (handlers[i].func != NULL)
                    handlers[i].func(&e);
                break;
            }
        }
        if (i<n)
            continue;

        switch(e.type) {
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
                warn("Unsupport event %d", e.type);
        }
    }
}

void
quit() {
    running = 0;
}

void 
clean() {
    int i, j;

    for (i=0; i<nworkspace; i++) {
        debug("clean workspace%d", i);
        workspace = i;
        for (j=0; j<nscreen; j++ ) {
            screen = j;
            client_exit();
        }
        free(clients[i]);
    }
    XDestroyWindow(display, empty);
    XFreeCursor(display, cursor);
    XUngrabKey(display, AnyKey, AnyModifier, root);
    XSync(display, False);
    XCloseDisplay(display);
}

int
main() {
    char *fn =
        malloc(strlen(getenv("HOME") + strlen("/.zwm") + 1)); 
    strcpy(fn, getenv("HOME"));
    strcat(fn, "/.zwm");
    if ( (log_fd = fopen(fn, "w")) == NULL ) {
        fprintf(stderr, "Can not open log file\n");
        return 1;
    }

    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
        die("can't set SIGCHLD");

    setup();
    run();
    clean();

    fclose(log_fd);
    free(fn);
    return 0;
}
