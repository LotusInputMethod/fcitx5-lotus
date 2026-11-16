/*
 * SPDX-FileCopyrightText: 2026 Kazoku <k4zoku@disr.it>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The shim forwards natively until keyboard capability is detected.
 * The C++ backend is linked into this ELF but initialized lazily.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <dlfcn.h>
#include <sched.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct wl_proxy;
struct wl_display;
struct wl_event_queue;
struct WlInterface;
union WlArgument;
struct WlMessage;
typedef int (*WlDispatcher)(const void*, void*, uint32_t, const struct WlMessage*, union WlArgument*);
typedef void (**WlListener)(void);

static _Atomic(int) runtime_active;
static _Atomic(int) loading_pid;
static _Atomic(int) loading_tid;
/* A forked child starts with native forwarding, then builds independent
 * process-local runtime/backend generations on keyboard activation. */
static _Atomic(int) runtime_owner_pid;
static _Atomic(int) recovering_child_pid;

extern int          lotus_apphook_resume_child_runtime(void) __attribute__((visibility("hidden")));
static void         adopt_metadata(void);
static void         activate(void);

static int          inherited_runtime(void) {
    const int owner = atomic_load_explicit(&runtime_owner_pid, memory_order_acquire);
    return owner && owner != (int)getpid();
}

/* Only the child which won the recovery CAS creates new generations.
 * Reentrant callers continue native forwarding until the switch completes. */
static void resume_fork_child(void) {
    const int pid = (int)getpid();
    if (!inherited_runtime())
        return;
    int previous = atomic_load_explicit(&recovering_child_pid, memory_order_acquire);
    if (previous && previous != pid)
        atomic_compare_exchange_strong_explicit(&recovering_child_pid, &previous, 0, memory_order_acq_rel, memory_order_acquire);
    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&recovering_child_pid, &expected, pid, memory_order_acq_rel, memory_order_acquire))
        return;
    if (lotus_apphook_resume_child_runtime()) {
        // Replay only child-created connection metadata. Native forwarding
        // remains in effect until the new generation has adopted it.
        adopt_metadata();
        atomic_store_explicit(&runtime_owner_pid, pid, memory_order_release);
        atomic_store_explicit(&runtime_active, 1, memory_order_release);
    }
    atomic_store_explicit(&recovering_child_pid, 0, memory_order_release);
}

static int runtime_enabled(void) {
    return !inherited_runtime() && atomic_load_explicit(&runtime_active, memory_order_acquire);
}

/* Called only from an inherited wl_keyboard callback, not from every dispatch.
 * Reactivate the child on its existing keyboard proxy without re-registering
 * libwayland callbacks or touching parent-owned mutexes. */
__attribute__((visibility("hidden"))) int lotus_apphook_activate_inherited_keyboard(void) {
    activate();
    return runtime_enabled();
}

/* Keep only connection metadata needed when the runtime is first activated. */
struct xcb_connection_record {
    xcb_connection_t*             connection;
    int                           screen;
    struct xcb_connection_record* next;
};
struct xlib_owner_record {
    Display*                  display;
    int                       owner;
    struct xlib_owner_record* next;
};
static struct xcb_connection_record* xcb_records;
static struct xlib_owner_record*     owner_records;
static _Atomic(int)                  metadata_lock_pid;
static int                           metadata_epoch_pid;

static void                          free_records(void) {
    while (xcb_records) {
        struct xcb_connection_record* old = xcb_records;
        xcb_records                       = old->next;
        free(old);
    }
    while (owner_records) {
        struct xlib_owner_record* old = owner_records;
        owner_records                 = old->next;
        free(old);
    }
}

static void lock_metadata(void) {
    int pid = (int)getpid();
    for (;;) {
        int owner = atomic_load_explicit(&metadata_lock_pid, memory_order_acquire);
        if (owner && owner != pid) {
            (void)atomic_compare_exchange_weak_explicit(&metadata_lock_pid, &owner, 0, memory_order_acq_rel, memory_order_acquire);
            continue;
        }
        if (owner) {
            sched_yield();
            continue;
        }
        int zero = 0;
        if (atomic_compare_exchange_weak_explicit(&metadata_lock_pid, &zero, pid, memory_order_acq_rel, memory_order_acquire))
            break;
    }
    // Never traverse a potentially mid-update inherited list after fork.
    if (metadata_epoch_pid != pid) {
        xcb_records        = NULL;
        owner_records      = NULL;
        metadata_epoch_pid = pid;
    }
}
static void unlock_metadata(void) {
    atomic_store_explicit(&metadata_lock_pid, 0, memory_order_release);
}

static void record_xcb(xcb_connection_t* c, int screen) {
    if (!c)
        return;
    lock_metadata();
    if (!runtime_enabled()) {
        for (struct xcb_connection_record* it = xcb_records; it; it = it->next) {
            if (it->connection == c) {
                it->screen = screen;
                unlock_metadata();
                return;
            }
        }
        struct xcb_connection_record* record = malloc(sizeof(*record));
        if (record) {
            *record     = (struct xcb_connection_record){.connection = c, .screen = screen, .next = xcb_records};
            xcb_records = record;
        }
    }
    unlock_metadata();
}
static void forget_xcb(xcb_connection_t* c) {
    lock_metadata();
    struct xcb_connection_record** cursor = &xcb_records;
    while (*cursor) {
        if ((*cursor)->connection == c) {
            struct xcb_connection_record* old = *cursor;
            *cursor                           = old->next;
            free(old);
            break;
        }
        cursor = &(*cursor)->next;
    }
    unlock_metadata();
}
static void record_owner(Display* d, int owner) {
    if (!d)
        return;
    lock_metadata();
    if (!runtime_enabled()) {
        for (struct xlib_owner_record* it = owner_records; it; it = it->next) {
            if (it->display == d) {
                it->owner = owner;
                unlock_metadata();
                return;
            }
        }
        struct xlib_owner_record* record = malloc(sizeof(*record));
        if (record) {
            *record       = (struct xlib_owner_record){.display = d, .owner = owner, .next = owner_records};
            owner_records = record;
        }
    }
    unlock_metadata();
}
static void forget_owner(Display* d) {
    lock_metadata();
    struct xlib_owner_record** cursor = &owner_records;
    while (*cursor) {
        if ((*cursor)->display == d) {
            struct xlib_owner_record* old = *cursor;
            *cursor                       = old->next;
            free(old);
            break;
        }
        cursor = &(*cursor)->next;
    }
    unlock_metadata();
}

/* Backend entrypoints are linked into this DSO and never require dlopen(). */
extern void lotus_apphook_initialize_runtime(void) __attribute__((visibility("hidden")));
extern void lotus_apphook_adopt_xcb_connection(xcb_connection_t*, int) __attribute__((visibility("hidden")));
extern void lotus_apphook_adopt_xlib_owner(Display*, int) __attribute__((visibility("hidden")));

static void adopt_metadata(void) {
    lock_metadata();
    for (struct xcb_connection_record* it = xcb_records; it; it = it->next)
        lotus_apphook_adopt_xcb_connection(it->connection, it->screen);
    for (struct xlib_owner_record* it = owner_records; it; it = it->next)
        lotus_apphook_adopt_xlib_owner(it->display, it->owner);
    free_records();
    unlock_metadata();
}

static void activate(void) {
    if (inherited_runtime())
        resume_fork_child();
    if (inherited_runtime() || atomic_load_explicit(&runtime_active, memory_order_acquire))
        return;
    const int pid     = (int)getpid();
    int       unowned = 0;
    if (!atomic_compare_exchange_strong_explicit(&runtime_owner_pid, &unowned, pid, memory_order_acq_rel, memory_order_acquire) && unowned != pid)
        return;
    const int tid = (int)syscall(SYS_gettid);
    for (;;) {
        int owner = atomic_load_explicit(&loading_pid, memory_order_acquire);
        if (atomic_load_explicit(&runtime_active, memory_order_acquire))
            return;
        if (owner && owner != pid) {
            (void)atomic_compare_exchange_weak_explicit(&loading_pid, &owner, 0, memory_order_acq_rel, memory_order_acquire);
            continue;
        }
        if (owner) {
            // Reentrant calls made while initializing must forward natively.
            if (atomic_load_explicit(&loading_tid, memory_order_acquire) == tid)
                return;
            sched_yield();
            continue;
        }
        int empty = 0;
        if (atomic_compare_exchange_weak_explicit(&loading_pid, &empty, pid, memory_order_acq_rel, memory_order_acquire))
            break;
    }
    atomic_store_explicit(&loading_tid, tid, memory_order_release);
    lotus_apphook_initialize_runtime();
    adopt_metadata();
    atomic_store_explicit(&runtime_active, 1, memory_order_release);
    atomic_store_explicit(&loading_tid, 0, memory_order_release);
    atomic_store_explicit(&loading_pid, 0, memory_order_release);
}

/* RTLD_NEXT cannot resolve symbols from RTLD_LOCAL frontend libraries. */
static void* resolve_native(const char* symbol) {
    void* fn = dlsym(RTLD_NEXT, symbol);
    if (fn)
        return fn;

    const char* soname = NULL;
    if (strncmp(symbol, "xcb_", 4) == 0)
        soname = "libxcb.so.1";
    else if (strncmp(symbol, "wl_", 3) == 0)
        soname = "libwayland-client.so.0";
    else if (strcmp(symbol, "XSetEventQueueOwner") == 0)
        soname = "libX11-xcb.so.1";
    else if (symbol[0] == 'X')
        soname = "libX11.so.6";

    if (!soname)
        return NULL;
    /* Probe only an already-loaded library; do not activate a frontend. */
    void* handle = dlopen(soname, RTLD_LAZY | RTLD_NOLOAD | RTLD_LOCAL);
    if (!handle)
        return NULL;
    fn = dlsym(handle, symbol);
    dlclose(handle);
    return fn;
}

static void* native_function(const char* symbol, _Atomic(void*)* native) {
    void* fn = atomic_load_explicit(native, memory_order_acquire);
    if (!fn) {
        fn = resolve_native(symbol);
        if (fn)
            atomic_store_explicit(native, fn, memory_order_release);
    }
    return fn;
}

#define CORE(symbol) lotus_apphook_impl_##symbol
#define RELAY(ret, symbol, params, call, fallback, trigger)                                                                                                                        \
    extern ret CORE(symbol)                                                                                                                                                        \
    params __attribute__((visibility("hidden")));                                                                                                                                  \
    __attribute__((visibility("default"))) ret symbol params {                                                                                                                     \
        typedef ret(*Proc) params;                                                                                                                                                 \
        static _Atomic(void*) native;                                                                                                                                              \
        if ((trigger))                                                                                                                                                             \
            activate();                                                                                                                                                            \
        if (runtime_enabled())                                                                                                                                                     \
            return CORE(symbol) call;                                                                                                                                              \
        Proc fn = (Proc)native_function(#symbol, &native);                                                                                                                         \
        return fn ? fn call : (fallback);                                                                                                                                          \
    }

#define RELAY_VOID(symbol, params, call, trigger)                                                                                                                                  \
    extern void                                        CORE(symbol) params __attribute__((visibility("hidden")));                                                                  \
    __attribute__((visibility("default"))) void symbol params {                                                                                                                    \
        typedef void(*Proc) params;                                                                                                                                                \
        static _Atomic(void*) native;                                                                                                                                              \
        if ((trigger))                                                                                                                                                             \
            activate();                                                                                                                                                            \
        if (runtime_enabled()) {                                                                                                                                                   \
            CORE(symbol) call;                                                                                                                                                     \
            return;                                                                                                                                                                \
        }                                                                                                                                                                          \
        Proc fn = (Proc)native_function(#symbol, &native);                                                                                                                         \
        if (fn)                                                                                                                                                                    \
            fn call;                                                                                                                                                               \
    }

#define XKEY_MASK (KeyPressMask | KeyReleaseMask)
RELAY(int, XSelectInput, (Display * d, Window w, long mask), (d, w, mask), 0, (mask & XKEY_MASK))
RELAY(int, XChangeWindowAttributes, (Display * d, Window w, unsigned long mask, XSetWindowAttributes* a), (d, w, mask, a), 0,
      ((mask & CWEventMask) && a && (a->event_mask & XKEY_MASK)))
RELAY(Window, XCreateWindow,
      (Display * d, Window p, int x, int y, unsigned int width, unsigned int height, unsigned int border, int depth, unsigned int cls, Visual* v, unsigned long mask,
       XSetWindowAttributes* a),
      (d, p, x, y, width, height, border, depth, cls, v, mask, a), None, ((mask & CWEventMask) && a && (a->event_mask & XKEY_MASK)))
__attribute__((visibility("default"))) void XSetEventQueueOwner(Display* d, int owner) {
    typedef void (*Proc)(Display*, int);
    static _Atomic(void*) native;
    extern void           CORE(XSetEventQueueOwner)(Display*, int) __attribute__((visibility("hidden")));
    record_owner(d, owner);
    if (runtime_enabled()) {
        CORE(XSetEventQueueOwner)(d, owner);
        return;
    }
    Proc fn = (Proc)native_function("XSetEventQueueOwner", &native);
    if (fn)
        fn(d, owner);
}
__attribute__((visibility("default"))) int XCloseDisplay(Display* d) {
    typedef int (*Proc)(Display*);
    static _Atomic(void*) native;
    extern int            CORE(XCloseDisplay)(Display*) __attribute__((visibility("hidden")));
    forget_owner(d);
    if (runtime_enabled())
        return CORE(XCloseDisplay)(d);
    Proc fn = (Proc)native_function("XCloseDisplay", &native);
    return fn ? fn(d) : -1;
}
RELAY(int, XDestroyWindow, (Display * d, Window w), (d, w), 0, 0)
RELAY(int, XPending, (Display * d), (d), 0, 0)
RELAY(int, XNextEvent, (Display * d, XEvent* ev), (d, ev), -1, 0)
RELAY(int, XPeekEvent, (Display * d, XEvent* ev), (d, ev), -1, 0)
RELAY(int, XMaskEvent, (Display * d, long mask, XEvent* ev), (d, mask, ev), -1, 0)
RELAY(int, XWindowEvent, (Display * d, Window w, long mask, XEvent* ev), (d, w, mask, ev), -1, 0)
RELAY(Bool, XCheckMaskEvent, (Display * d, long mask, XEvent* ev), (d, mask, ev), False, 0)
RELAY(Bool, XCheckWindowEvent, (Display * d, Window w, long mask, XEvent* ev), (d, w, mask, ev), False, 0)
RELAY(Bool, XCheckTypedEvent, (Display * d, int type, XEvent* ev), (d, type, ev), False, 0)
RELAY(Bool, XCheckTypedWindowEvent, (Display * d, Window w, int type, XEvent* ev), (d, w, type, ev), False, 0)
RELAY(int, XIfEvent, (Display * d, XEvent* ev, Bool (*pred)(Display*, XEvent*, XPointer), XPointer arg), (d, ev, pred, arg), -1, 0)
RELAY(Bool, XCheckIfEvent, (Display * d, XEvent* ev, Bool (*pred)(Display*, XEvent*, XPointer), XPointer arg), (d, ev, pred, arg), False, 0)
RELAY(int, XPeekIfEvent, (Display * d, XEvent* ev, Bool (*pred)(Display*, XEvent*, XPointer), XPointer arg), (d, ev, pred, arg), -1, 0)

static int subscribes_keyboard(uint32_t mask, const void* values) {
    if (!(mask & XCB_CW_EVENT_MASK) || !values)
        return 0;
    const uint32_t* items = values;
    unsigned int    index = __builtin_popcount(mask & (XCB_CW_EVENT_MASK - 1));
    return (items[index] & (XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE)) != 0;
}

/* Opening an X connection alone is not evidence of a keyboard frontend. */
__attribute__((visibility("default"))) xcb_connection_t* xcb_connect(const char* display, int* screen) {
    typedef xcb_connection_t* (*Proc)(const char*, int*);
    static _Atomic(void*) native;
    Proc                  fn         = (Proc)native_function("xcb_connect", &native);
    int                   selected   = 0;
    int*                  screen_out = screen ? screen : &selected;
    xcb_connection_t*     c          = fn ? fn(display, screen_out) : NULL;
    if (c) {
        record_xcb(c, *screen_out);
        // Connection may have opened while another thread activated runtime.
        if (runtime_enabled())
            lotus_apphook_adopt_xcb_connection(c, *screen_out);
    }
    return c;
}
__attribute__((visibility("default"))) xcb_connection_t* xcb_connect_to_fd(int fd, xcb_auth_info_t* auth) {
    typedef xcb_connection_t* (*Proc)(int, xcb_auth_info_t*);
    static _Atomic(void*) native;
    Proc                  fn         = (Proc)native_function("xcb_connect_to_fd", &native);
    xcb_connection_t*     connection = fn ? fn(fd, auth) : NULL;
    if (connection) {
        record_xcb(connection, -1);
        if (runtime_enabled())
            lotus_apphook_adopt_xcb_connection(connection, -1);
    }
    return connection;
}
RELAY(xcb_void_cookie_t, xcb_destroy_window, (xcb_connection_t * c, xcb_window_t w), (c, w), (xcb_void_cookie_t){0}, 0)
RELAY(xcb_void_cookie_t, xcb_destroy_window_checked, (xcb_connection_t * c, xcb_window_t w), (c, w), (xcb_void_cookie_t){0}, 0)
__attribute__((visibility("default"))) void xcb_disconnect(xcb_connection_t* c) {
    typedef void (*Proc)(xcb_connection_t*);
    static _Atomic(void*) native;
    extern void           CORE(xcb_disconnect)(xcb_connection_t*) __attribute__((visibility("hidden")));
    forget_xcb(c);
    if (runtime_enabled()) {
        CORE(xcb_disconnect)(c);
        return;
    }
    Proc fn = (Proc)native_function("xcb_disconnect", &native);
    if (fn)
        fn(c);
}
RELAY(xcb_generic_event_t*, xcb_poll_for_event, (xcb_connection_t * c), (c), NULL, 0)
RELAY(xcb_generic_event_t*, xcb_poll_for_queued_event, (xcb_connection_t * c), (c), NULL, 0)
RELAY(xcb_generic_event_t*, xcb_wait_for_event, (xcb_connection_t * c), (c), NULL, 0)
RELAY(xcb_void_cookie_t, xcb_change_window_attributes, (xcb_connection_t * c, xcb_window_t w, uint32_t mask, const void* v), (c, w, mask, v), (xcb_void_cookie_t){0},
      subscribes_keyboard(mask, v))
RELAY(xcb_void_cookie_t, xcb_change_window_attributes_checked, (xcb_connection_t * c, xcb_window_t w, uint32_t mask, const void* v), (c, w, mask, v), (xcb_void_cookie_t){0},
      subscribes_keyboard(mask, v))
RELAY(xcb_void_cookie_t, xcb_create_window,
      (xcb_connection_t * c, uint8_t d, xcb_window_t w, xcb_window_t p, int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t border, uint16_t cls, xcb_visualid_t visual,
       uint32_t mask, const void* v),
      (c, d, w, p, x, y, width, height, border, cls, visual, mask, v), (xcb_void_cookie_t){0}, subscribes_keyboard(mask, v))
RELAY(xcb_void_cookie_t, xcb_create_window_checked,
      (xcb_connection_t * c, uint8_t d, xcb_window_t w, xcb_window_t p, int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t border, uint16_t cls, xcb_visualid_t visual,
       uint32_t mask, const void* v),
      (c, d, w, p, x, y, width, height, border, cls, visual, mask, v), (xcb_void_cookie_t){0}, subscribes_keyboard(mask, v))

/* Listener/dispatcher registration is the first keyboard-specific Wayland call. */
static int is_keyboard(struct wl_proxy* proxy) {
    typedef const char* (*GetClass)(struct wl_proxy*);
    static _Atomic(void*) native;
    GetClass              fn = (GetClass)atomic_load_explicit(&native, memory_order_acquire);
    if (!fn) {
        fn = (GetClass)resolve_native("wl_proxy_get_class");
        if (fn)
            atomic_store_explicit(&native, (void*)fn, memory_order_release);
    }
    if (!proxy || !fn)
        return 0;
    const char* cls = fn(proxy);
    return cls && strcmp(cls, "wl_keyboard") == 0;
}
RELAY(int, wl_proxy_add_listener, (struct wl_proxy * p, void (**callbacks)(void), void* data), (p, callbacks, data), -1, is_keyboard(p))
RELAY(int, wl_proxy_add_dispatcher, (struct wl_proxy * p, WlDispatcher disp, const void* impl, void* data), (p, disp, impl, data), -1, is_keyboard(p))
extern WlListener       CORE(wl_proxy_get_listener)(struct wl_proxy*) __attribute__((visibility("hidden")));
extern struct wl_proxy* CORE(wl_proxy_marshal_array_flags)(struct wl_proxy*, uint32_t, const struct WlInterface*, uint32_t, uint32_t, union WlArgument*)
    __attribute__((visibility("hidden")));
extern void                                       CORE(wl_proxy_destroy)(struct wl_proxy*) __attribute__((visibility("hidden")));
extern void                                       CORE(wl_display_disconnect)(struct wl_display*) __attribute__((visibility("hidden")));
extern int                                        lotus_apphook_inherited_wayland_listener(struct wl_proxy*, WlListener*) __attribute__((visibility("hidden")));
extern void                                       lotus_apphook_retire_wayland_proxy(struct wl_proxy*) __attribute__((visibility("hidden")));
extern void                                       lotus_apphook_retire_wayland_display(struct wl_display*) __attribute__((visibility("hidden")));

__attribute__((visibility("default"))) WlListener wl_proxy_get_listener(struct wl_proxy* p) {
    typedef WlListener (*Proc)(struct wl_proxy*);
    static _Atomic(void*) native;
    // In a forked child the native proxy still exposes the parent's wrapper.
    if (inherited_runtime()) {
        WlListener listener;
        if (lotus_apphook_inherited_wayland_listener(p, &listener))
            return listener;
    }
    if (runtime_enabled())
        return CORE(wl_proxy_get_listener)(p);
    Proc fn = (Proc)native_function("wl_proxy_get_listener", &native);
    return fn ? fn(p) : NULL;
}
__attribute__((visibility("default"))) struct wl_proxy* wl_proxy_marshal_array_flags(struct wl_proxy* p, uint32_t op, const struct WlInterface* i, uint32_t version, uint32_t flags,
                                                                                     union WlArgument* args) {
    typedef struct wl_proxy* (*Proc)(struct wl_proxy*, uint32_t, const struct WlInterface*, uint32_t, uint32_t, union WlArgument*);
    static _Atomic(void*) native;
    struct wl_proxy*      result;
    if (runtime_enabled())
        return CORE(wl_proxy_marshal_array_flags)(p, op, i, version, flags, args);
    Proc fn = (Proc)native_function("wl_proxy_marshal_array_flags", &native);
    result  = fn ? fn(p, op, i, version, flags, args) : NULL;
    if (fn && (flags & 1U))
        lotus_apphook_retire_wayland_proxy(p);
    return result;
}
__attribute__((visibility("default"))) void wl_proxy_destroy(struct wl_proxy* p) {
    typedef void (*Proc)(struct wl_proxy*);
    static _Atomic(void*) native;
    if (runtime_enabled()) {
        CORE(wl_proxy_destroy)(p);
        return;
    }
    Proc fn = (Proc)native_function("wl_proxy_destroy", &native);
    lotus_apphook_retire_wayland_proxy(p);
    if (fn)
        fn(p);
}
__attribute__((visibility("default"))) void wl_display_disconnect(struct wl_display* d) {
    typedef void (*Proc)(struct wl_display*);
    static _Atomic(void*) native;
    if (runtime_enabled()) {
        CORE(wl_display_disconnect)(d);
        return;
    }
    Proc fn = (Proc)native_function("wl_display_disconnect", &native);
    lotus_apphook_retire_wayland_display(d);
    if (fn)
        fn(d);
}
RELAY(int, wl_display_dispatch_queue_pending, (struct wl_display * d, struct wl_event_queue* q), (d, q), -1, 0)
RELAY(int, wl_display_dispatch_pending, (struct wl_display * d), (d), -1, 0)
RELAY(int, wl_display_dispatch, (struct wl_display * d), (d), -1, 0)
RELAY(int, wl_display_dispatch_queue, (struct wl_display * d, struct wl_event_queue* q), (d, q), -1, 0)
/* Optional Wayland dispatch APIs must not be exported unconditionally: an app
 * may use dlsym() to detect their presence. The native libwayland-client
 * handles these calls directly when it supports them. */
RELAY(struct wl_display*, wl_display_connect, (const char* name), (name), NULL, 0)
RELAY(struct wl_display*, wl_display_connect_to_fd, (int fd), (fd), NULL, 0)
