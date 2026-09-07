#include "host_mac.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Files.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Processes.h>
#include <string.h>
#include <limits.h>

struct cb_host_context {
    uint32_t *saved_sp;
    void *stack;
    void (*entry)(void *);
    void *argument;
};

extern void cb_mac_context_swap(struct cb_host_context *, struct cb_host_context *);
extern void cb_mac_context_seed(uint32_t *);

static WindowPtr window;
static char display_text[8192];
static size_t display_used;
static char captured[4096];
static size_t captured_used;
static int capturing;
static int quit_requested;
/* Canonical input: a partial line is editable until Return is pressed. */
static char input[1024];
static size_t input_used, input_ready, input_read;
static uint32_t last_ticks;
static uint64_t elapsed_ticks;

static void redraw(void)
{
    size_t start = 0, index;
    int lines = 0, row = 0, column = 0;
    if (window == NULL) return;
    SetPort(window);
    EraseRect(&window->portRect);
    TextFont(4); TextSize(9); /* Monaco's standard font ID. */
    for (index = 0; index < display_used; ++index)
        if (display_text[index] == '\n') ++lines;
    while (lines > 29 && start < display_used)
        if (display_text[start++] == '\n') --lines;
    MoveTo(8, 14);
    for (index = start; index < display_used && row < 31; ++index) {
        unsigned char ch = (unsigned char)display_text[index];
        if (ch == '\n' || column == 100) {
            MoveTo(8, (short)(14 + 12 * ++row));
            column = 0;
            if (ch == '\n') continue;
        }
        DrawChar(ch);
        ++column;
    }
}

static void append_text(const char *text, size_t count)
{
    size_t index;
    for (index = 0; index < count; ++index) {
        char ch = text[index];
        if (ch == '\r') ch = '\n';
        if (ch == '\b') {
            if (display_used && display_text[display_used - 1] != '\n')
                --display_used;
            continue;
        }
        if (display_used == sizeof(display_text) - 1) {
            memmove(display_text, display_text + 1024, display_used - 1024);
            display_used -= 1024;
        }
        display_text[display_used++] = ch;
    }
    display_text[display_used] = '\0';
    if (window != NULL) InvalRect(&window->portRect);
}

void cb_mac_text(const char *text) { append_text(text, strlen(text)); }

static void *host_allocate(size_t size)
{
    if (size > INT32_MAX) return NULL;
    return NewPtr((Size)(size ? size : 1));
}

static void host_release(void *pointer)
{
    if (pointer != NULL) DisposePtr((Ptr)pointer);
}

static void *host_resize(void *pointer, size_t size)
{
    void *replacement;
    Size old_size;
    if (pointer == NULL) return host_allocate(size);
    if (size == 0) { host_release(pointer); return NULL; }
    replacement = host_allocate(size);
    if (replacement == NULL) return NULL;
    old_size = GetPtrSize((Ptr)pointer);
    memcpy(replacement, pointer, (size_t)old_size < size ? (size_t)old_size : size);
    host_release(pointer);
    return replacement;
}

static void host_fatal(const char *message)
{
    cb_mac_text("\nFATAL: "); cb_mac_text(message); cb_mac_text("\n");
    cb_mac_write_result(display_text);
    while (!quit_requested) cb_mac_pump(-1);
    ExitToShell();
}

static void context_entry(struct cb_host_context *context)
{
    context->entry(context->argument);
    host_fatal("a stackful context returned");
}

static struct cb_host_context *context_root(void)
{
    struct cb_host_context *context = host_allocate(sizeof(*context));
    if (context != NULL) memset(context, 0, sizeof(*context));
    return context;
}

static struct cb_host_context *context_create(void (*entry)(void *),
                                             void *arg, size_t stack_size)
{
    struct cb_host_context *context;
    uint32_t *frame;
    if (entry == NULL || stack_size < 1024 || stack_size > INT32_MAX - 4)
        return NULL;
    context = context_root();
    if (context == NULL) return NULL;
    context->stack = host_allocate(stack_size + 4);
    if (context->stack == NULL) { host_release(context); return NULL; }
    context->entry = entry;
    context->argument = arg;
    frame = (uint32_t *)(((uintptr_t)context->stack + stack_size) & ~(uintptr_t)3);
    frame -= 14; /* 11 saved registers, entry PC, return PC, C argument. */
    cb_mac_context_seed(frame);
    frame[11] = (uint32_t)(uintptr_t)context_entry;
    frame[12] = 0; /* context_entry must not return. */
    frame[13] = (uint32_t)(uintptr_t)context;
    context->saved_sp = frame;
    return context;
}

static void context_destroy(struct cb_host_context *context)
{
    if (context == NULL) return;
    host_release(context->stack);
    host_release(context);
}

void cb_mac_pump(int timeout_ms)
{
    EventRecord event;
    unsigned long ticks = timeout_ms < 0 ? 1 : (unsigned long)timeout_ms * 60 / 1000;
    if (WaitNextEvent(everyEvent, &event, ticks, NULL)) {
        if (event.what == updateEvt) {
            BeginUpdate(window); redraw(); EndUpdate(window);
        } else if (event.what == keyDown || event.what == autoKey) {
            char ch = (char)(event.message & charCodeMask);
            if ((event.modifiers & cmdKey) && (ch == 'q' || ch == 'Q')) {
                quit_requested = 1;
            } else if (!(event.modifiers & cmdKey) && input_ready == 0) {
                if (ch == '\b' || ch == 127) {
                    if (input_used) { --input_used; append_text("\b", 1); }
                } else if (ch == '\r' && input_used < sizeof(input) - 1) {
                    input[input_used++] = '\n';
                    input_ready = input_used;
                    append_text("\n", 1);
                } else if (ch >= 32 && ch < 127 && input_used < sizeof(input) - 2) {
                    input[input_used++] = ch;
                    append_text(&ch, 1);
                }
            }
        } else if (event.what == mouseDown) {
            WindowPtr hit;
            short part = FindWindow(event.where, &hit);
            if (part == inGoAway && TrackGoAway(hit, event.where))
                quit_requested = 1;
            else if (part == inDrag) {
                Rect bounds = qd.screenBits.bounds;
                DragWindow(hit, event.where, &bounds);
            } else if (part == inContent) SelectWindow(hit);
        }
    }
    if (window != NULL) SetPort(window);
}

static int console_poll(int timeout_ms)
{
    cb_mac_pump(timeout_ms);
    return quit_requested || input_ready > input_read;
}

static cb_ssize_t console_read(void *buffer, size_t count)
{
    size_t available = input_ready - input_read;
    if (count > available) count = available;
    memcpy(buffer, input + input_read, count);
    input_read += count;
    if (input_ready && input_read == input_ready)
        input_used = input_ready = input_read = 0;
    return (cb_ssize_t)count;
}

static cb_ssize_t console_write(int stream, const void *buffer, size_t count)
{
    (void)stream;
    if (capturing) {
        size_t room = sizeof(captured) - 1 - captured_used;
        size_t copied = count < room ? count : room;
        memcpy(captured + captured_used, buffer, copied);
        captured_used += copied;
        captured[captured_used] = '\0';
    }
    append_text(buffer, count);
    return (cb_ssize_t)count;
}

static uint64_t monotonic_millis(void)
{
    uint32_t ticks = TickCount();
    elapsed_ticks += (uint32_t)(ticks - last_ticks);
    last_ticks = ticks;
    return elapsed_ticks * 1000 / 60;
}

/* The classic clock is local civil time, with no reliable UTC offset in
 * this first backend. v1 explicitly permits zero for unavailable UTC. */
static uint64_t wall_clock_millis(void) { return 0; }
static void yield_host(void) { cb_mac_pump(1); }

static const struct cb_host_ops_v1 mac_ops = {
    CB_ABI_VERSION_V1, sizeof(mac_ops),
    host_allocate, host_resize, host_release,
    context_root, context_create, cb_mac_context_swap, context_destroy,
    console_poll, console_read, console_write,
    monotonic_millis, wall_clock_millis, yield_host, host_fatal
};

const struct cb_host_ops_v1 *cb_mac_host_ops(void) { return &mac_ops; }
void cb_mac_capture_begin(void) { capturing = 1; captured_used = 0; captured[0] = 0; }
const char *cb_mac_capture(void) { capturing = 0; return captured; }
int cb_mac_quitting(void) { return quit_requested; }

int cb_mac_initialize(void)
{
    Rect bounds = {45, 30, 445, 660};
    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus();
    TEInit(); InitDialogs(NULL); InitCursor();
    window = NewWindow(NULL, &bounds, (ConstStringPtr)"\pcannedBSD - System 7 / 68K",
                       true, documentProc, (WindowPtr)-1, true, 0);
    if (window == NULL) return -1;
    SetPort(window);
    last_ticks = TickCount();
    return 0;
}

int cb_mac_write_result(const char *text)
{
    Str255 path = "\pUnix:cannedbsd-result.txt";
    short reference, volume;
    long length = (long)strlen(text), written = length;
    OSErr error;
    HCreate(0, 0, path, 'CnBD', 'TEXT');
    if (HOpenDF(0, 0, path, fsRdWrPerm, &reference) != noErr) return -1;
    error = SetEOF(reference, 0);
    if (error == noErr) error = FSWrite(reference, &written, text);
    if (GetVRefNum(reference, &volume) != noErr) volume = 0;
    if (FSClose(reference) != noErr) error = ioErr;
    if (volume && FlushVol(NULL, volume) != noErr) error = ioErr;
    return error == noErr && written == length ? 0 : -1;
}

void cb_mac_shutdown(void) { if (window != NULL) DisposeWindow(window); }
