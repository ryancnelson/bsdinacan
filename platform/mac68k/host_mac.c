#include "host_mac.h"
#include "acceptance_output.h"
#include "root_dispatch.h"

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
#include <stddef.h>

struct cb_host_context {
    uint32_t *saved_sp;
    uint32_t saved_stack_low; /* context.S offset 4: System 7 StkLowPt. */
    void *stack;
    void (*entry)(void *);
    void *argument;
};

typedef char context_stack_low_offset[
    offsetof(struct cb_host_context, saved_stack_low) == 4 ? 1 : -1];
extern void cb_mac_context_seed(uint32_t *);

static WindowPtr window;
static char display_text[8192];
static size_t display_used;
static char captured[4096];
static size_t captured_used;
static int captured_truncated;
static int capturing;
static int quit_requested;
/* Canonical input: a partial line is editable until Return is pressed. */
static char input[1024];
static size_t input_used, input_ready, input_read;
static uint32_t last_ticks;
static uint64_t elapsed_ticks;
static struct cb_mac_dispatch dispatch;
static int display_dirty;

/* Toolbox requests are serviced on the original application stack and then
 * resume the requesting task without returning to the kernel scheduler. */
static void on_root(void (*function)(void *), void *argument)
{
    cb_mac_dispatch_call(&dispatch, function, argument);
}

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
    display_dirty = 1;
}

void cb_mac_text(const char *text) { append_text(text, strlen(text)); }

struct memory_call { void *pointer; size_t size; void *result; };

static void allocate_on_root(void *argument)
{
    struct memory_call *call = argument;
    call->result = NewPtr((Size)(call->size ? call->size : 1));
}

static void *host_allocate(size_t size)
{
    struct memory_call call = {NULL, size, NULL};
    if (size > INT32_MAX) return NULL;
    on_root(allocate_on_root, &call);
    return call.result;
}

static void release_on_root(void *pointer) { DisposePtr((Ptr)pointer); }
static void host_release(void *pointer)
{
    if (pointer != NULL) on_root(release_on_root, pointer);
}

static void resize_on_root(void *argument)
{
    struct memory_call *call = argument;
    Size old_size;
    call->result = host_allocate(call->size);
    if (call->result == NULL) return;
    old_size = GetPtrSize((Ptr)call->pointer);
    memcpy(call->result, call->pointer,
           (size_t)old_size < call->size ? (size_t)old_size : call->size);
    host_release(call->pointer);
}

static void *host_resize(void *pointer, size_t size)
{
    struct memory_call call = {pointer, size, NULL};
    if (pointer == NULL) return host_allocate(size);
    if (size == 0) { host_release(pointer); return NULL; }
    if (size > INT32_MAX) return NULL;
    on_root(resize_on_root, &call);
    return call.result;
}

static void fatal_on_root(void *argument)
{
    const char *message = argument;
    cb_mac_text("\nFATAL: "); cb_mac_text(message); cb_mac_text("\n");
    cb_mac_write_result(display_text);
    while (!quit_requested) cb_mac_pump(-1);
    ExitToShell();
}

static void host_fatal(const char *message)
{
    on_root(fatal_on_root, (void *)message);
}

static void context_entry(struct cb_host_context *context)
{
    context->entry(context->argument);
    host_fatal("a stackful context returned");
}

static struct cb_host_context *new_context(void)
{
    struct cb_host_context *context = host_allocate(sizeof(*context));
    if (context != NULL) memset(context, 0, sizeof(*context));
    return context;
}

static struct cb_host_context *context_root(void)
{
    dispatch.root = new_context();
    dispatch.active = dispatch.root;
    return dispatch.root;
}

static void context_switch(struct cb_host_context *from,
                           struct cb_host_context *to)
{
    cb_mac_dispatch_switch(&dispatch, from, to);
}

static struct cb_host_context *context_create(void (*entry)(void *),
                                             void *arg, size_t stack_size)
{
    struct cb_host_context *context;
    uint32_t *frame;
    if (entry == NULL || stack_size < 1024 || stack_size > INT32_MAX - 4)
        return NULL;
    context = new_context();
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
    if (context == dispatch.root) dispatch.root = dispatch.active = NULL;
    host_release(context->stack);
    host_release(context);
}

void cb_mac_pump(int timeout_ms)
{
    EventRecord event;
    unsigned long ticks = timeout_ms < 0 ? 1 : (unsigned long)timeout_ms * 60 / 1000;
    /* Toolbox event handling belongs on the original application stack.
     * The core polls there before waking blocked readers; task-side polls
     * only inspect already buffered input. VBL sniffer state is managed
     * separately by the assembly context boundary. */
    if (dispatch.active != dispatch.root) return;
    if (display_dirty && window != NULL) {
        InvalRect(&window->portRect);
        display_dirty = 0;
    }
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
        if (copied != count) captured_truncated = 1;
        memcpy(captured + captured_used, buffer, copied);
        captured_used += copied;
        captured[captured_used] = '\0';
    }
    append_text(buffer, count);
    return (cb_ssize_t)count;
}

static void ticks_on_root(void *result) { *(uint32_t *)result = TickCount(); }
static uint64_t monotonic_millis(void)
{
    uint32_t ticks;
    on_root(ticks_on_root, &ticks);
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
    context_root, context_create, context_switch, context_destroy,
    console_poll, console_read, console_write,
    monotonic_millis, wall_clock_millis, yield_host, host_fatal
};

const struct cb_host_ops_v1 *cb_mac_host_ops(void) { return &mac_ops; }
void cb_mac_capture_begin(void)
{
    capturing = 1;
    captured_used = 0;
    captured_truncated = 0;
    captured[0] = 0;
}
int cb_mac_capture_matches(const char *expected)
{
    capturing = 0;
    return cb_acceptance_output_matches(captured, captured_used,
                                        captured_truncated, expected);
}
int cb_mac_quitting(void) { return quit_requested; }

int cb_mac_initialize(void)
{
    Rect bounds = {45, 30, 445, 660};
    /* Expand the application heap while still on the original Mac stack.
     * Later coroutine stacks live in heap blocks; heap growth must not infer
     * its upper limit from one of those temporarily active stack pointers. */
    MaxApplZone();
    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus();
    TEInit(); InitDialogs(NULL); InitCursor();
    window = NewWindow(NULL, &bounds, (ConstStringPtr)"\pcannedBSD - System 7 / 68K",
                       true, documentProc, (WindowPtr)-1, true, 0);
    if (window == NULL) return -1;
    SetPort(window);
    last_ticks = TickCount();
    return 0;
}

static int write_evidence_file(ConstStr255Param path, OSType type,
                               const void *prefix, long prefix_size,
                               const void *bytes, long size)
{
    short reference, volume = 0;
    long written;
    OSErr error;
    if (dispatch.active != dispatch.root) return -1;
    HCreate(0, 0, path, 'CnBD', type);
    if (HOpenDF(0, 0, path, fsRdWrPerm, &reference) != noErr) return -1;
    error = GetVRefNum(reference, &volume);
    if (error == noErr && volume == 0) error = ioErr;
    if (error == noErr) error = SetEOF(reference, 0);
    if (error == noErr && prefix_size) {
        written = prefix_size;
        error = FSWrite(reference, &written, prefix);
        if (written != prefix_size) error = ioErr;
    }
    if (error == noErr && size) {
        written = size;
        error = FSWrite(reference, &written, bytes);
        if (written != size) error = ioErr;
    }
    if (FSClose(reference) != noErr) error = ioErr;
    if (volume && FlushVol(NULL, volume) != noErr) error = ioErr;
    return error == noErr ? 0 : -1;
}

int cb_mac_write_result(const char *text)
{
    Str255 path = "\pUnix:cannedbsd-result.txt";
    return write_evidence_file(path, 'TEXT', NULL, 0, text, (long)strlen(text));
}

int cb_mac_autorun_requested(void)
{
    Str255 path = "\pUnix:cannedbsd-autorun.txt";
    short reference;
    OSErr error = HOpenDF(0, 0, path, fsRdPerm, &reference);
    if (error == fnfErr || error == nsvErr) return 0;
    if (error != noErr) return -1;
    return FSClose(reference) == noErr ? 1 : -1;
}

int cb_mac_write_done(const char *text)
{
    Str255 path = "\pUnix:cannedbsd-done.txt";
    int status = write_evidence_file(path, 'TEXT', NULL, 0,
                                     text, (long)strlen(text));
    /* Do not leave a useful completion token after an observed write failure.
     * The controller also requires application exit, since this cleanup can
     * itself fail on a broken shared volume. */
    if (status < 0) write_evidence_file(path, 'TEXT', NULL, 0, NULL, 0);
    return status;
}

int cb_mac_capture_screen(void)
{
    Str255 path = "\pUnix:cannedbsd-screen.pict";
    static const char header[512] = {0};
    RGBColor black = {0, 0, 0}, white = {65535, 65535, 65535};
    OpenCPicParams parameters = {0};
    CGrafPtr saved_port;
    GDHandle saved_device;
    GWorldPtr pixels = NULL;
    PixMapHandle pixel_map;
    PicHandle picture = NULL;
    Size size;
    int status = -1;
    if (window == NULL || dispatch.active != dispatch.root) return -1;
    /* Snapshot the actual displayed pixels, not text drawing commands.
     * A direct-color PICT avoids legacy 1-bit bitmap decoder incompatibilities. */
    SelectWindow(window);
    redraw();
    ValidRect(&window->portRect);
    GetGWorld(&saved_port, &saved_device);
    if (NewGWorld(&pixels, 32, &window->portRect, NULL, NULL, 0) != noErr)
        return -1;
    pixel_map = GetGWorldPixMap(pixels);
    if (!LockPixels(pixel_map)) goto dispose_pixels;
    SetGWorld(pixels, NULL);
    RGBForeColor(&black);
    RGBBackColor(&white);
    ClipRect(&window->portRect);
    CopyBits(&window->portBits, &((GrafPtr)pixels)->portBits,
             &window->portRect, &window->portRect, srcCopy, NULL);
    if (QDError() != noErr) goto restore_port;
    parameters.srcRect = window->portRect;
    parameters.hRes = parameters.vRes = 72L << 16;
    parameters.version = -2;
    picture = OpenCPicture(&parameters);
    if (picture == NULL) goto restore_port;
    /* The default port clip is unbounded; record the actual image frame. */
    ClipRect(&parameters.srcRect);
    CopyBits(&((GrafPtr)pixels)->portBits, &((GrafPtr)pixels)->portBits,
             &parameters.srcRect, &parameters.srcRect, srcCopy, NULL);
    ClosePicture();
    size = GetHandleSize((Handle)picture);
    if (QDError() == noErr && size > (Size)sizeof(Picture)) {
        HLock((Handle)picture);
        if (MemError() == noErr)
            status = write_evidence_file(path, 'PICT', header, sizeof(header),
                                         *picture, size);
        HUnlock((Handle)picture);
    }
    KillPicture(picture);
restore_port:
    SetGWorld(saved_port, saved_device);
    UnlockPixels(pixel_map);
dispose_pixels:
    DisposeGWorld(pixels);
    return status;
}

void cb_mac_shutdown(void) { if (window != NULL) DisposeWindow(window); }
