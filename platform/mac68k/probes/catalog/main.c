#include "catalog.h"
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Memory.h>
#include <Errors.h>
#include <OSUtils.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "fixture_generated.h"
#ifndef PROBE_COMMIT
#error Exact commit must be supplied by the build
#endif

static WindowPtr window;
static char report[4096];
static int all_pass = 1;
struct capture {
    unsigned long before;
    struct cb_catalog_entry entries[8];
    unsigned int count, stop;
    unsigned long after;
};
static void initialize_capture(struct capture *c, unsigned int stop)
{
    memset(c, 0, sizeof(*c));
    c->before = c->after = 0x1234abcdUL;
    c->stop = stop;
}
static Boolean capture(const struct cb_catalog_entry *entry, void *data)
{
    struct capture *c = data;
    if (c->count >= 8) { c->after = 0; return true; }
    c->entries[c->count++] = *entry;
    return c->stop && c->count >= c->stop;
}
static int valid_capture(const struct capture *c)
{
    return c->before == 0x1234abcdUL && c->after == 0x1234abcdUL;
}
static int same_entry(const struct cb_catalog_entry *a, const struct cb_catalog_entry *b)
{
    return !strcmp(a->name, b->name) && a->id == b->id && a->parent_id == b->parent_id &&
        a->directory == b->directory && a->data_length == b->data_length &&
        a->resource_length == b->resource_length;
}
static int matches_fixture(const struct capture *c)
{
    unsigned int i, j, found = 0;
    if (!valid_capture(c) || c->count != FIXTURE_ROOT_COUNT) return 0;
    for (i = 0; i < c->count; ++i) {
        unsigned int mask = 0;
        for (j = 0; j < sizeof(fixture_entries) / sizeof(fixture_entries[0]); ++j)
            if (fixture_entries[j].parent_id == 2 && same_entry(&c->entries[i], &fixture_entries[j]))
                mask = 1U << j;
        if (!mask || (found & mask)) return 0;
        found |= mask;
    }
    return 1;
}
static void record(const char *name, int pass, OSErr error, unsigned int count)
{
    char line[160];
    snprintf(line, sizeof(line), "%s %s error=%d count=%u\n", pass ? "PASS" : "FAIL",
             name, (int)error, count);
    strcat(report, line);
    if (!pass) all_pass = 0;
}
static unsigned int query_calls;
static OSErr denied(CInfoPBPtr pb)
{
    (void)pb; ++query_calls;
    return -5000; /* afpAccessDenied, pinned MoreFiles.h documents this value. */
}
static OSErr endless(CInfoPBPtr pb)
{
    ++query_calls;
    pb->hFileInfo.ioNamePtr[0] = 1;
    pb->hFileInfo.ioNamePtr[1] = 'x';
    pb->hFileInfo.ioFlAttrib = 0;
    pb->hFileInfo.ioDirID = pb->hFileInfo.ioFDirIndex + 100;
    return noErr;
}
static OSErr fixture_volume(short *volume)
{
    HParamBlockRec pb;
    Str255 name = "\pCatalogFixture:";
    OSErr error;
    memset(&pb, 0, sizeof(pb));
    pb.volumeParam.ioNamePtr = name;
    pb.volumeParam.ioVolIndex = -1;
    error = PBHGetVInfoSync(&pb);
    if (error == noErr) *volume = pb.volumeParam.ioVRefNum;
    return error;
}
static void run_probe(void)
{
    short volume = 0;
    OSErr error, second_error;
    struct capture first, second, saved;
    struct cb_catalog_entry entry;
    error = fixture_volume(&volume);
    record("fixture-volume", error == noErr && volume != 0, error, 0);
    if (error != noErr || !volume) return;
    initialize_capture(&first, 0);
    error = cb_catalog_scan(volume, 2, 32, capture, &first);
    record("root-names-types-forks-ids", error == noErr && matches_fixture(&first), error, first.count);
    saved = first;
    initialize_capture(&second, 0);
    error = cb_catalog_scan(volume, 2, 32, capture, &second);
    record("second-scan-stable-ids", error == noErr && matches_fixture(&second), error, second.count);
    second.entries[0].name[0] = '?';
    record("independent-copied-buffers", !memcmp(&first, &saved, sizeof(first)), noErr, first.count);
    error = cb_catalog_lookup(volume, 2, (ConstStr255Param)"\pEmpty", &entry);
    initialize_capture(&second, 0);
    if (error == noErr) error = cb_catalog_scan(volume, entry.id, 32, capture, &second);
    record("empty-directory", error == noErr && second.count == 0 && valid_capture(&second), error, second.count);
    initialize_capture(&second, 1);
    error = cb_catalog_scan(volume, 2, 32, capture, &second);
    record("early-stop-callback", error == noErr && second.count == 1 && valid_capture(&second), error, second.count);
    initialize_capture(&second, 0);
    error = cb_catalog_scan(volume, 2, 32, capture, &second);
    record("full-scan-after-stop", error == noErr && matches_fixture(&second), error, second.count);
    error = cb_catalog_lookup(volume, 2, (ConstStr255Param)"\pMissing", &entry);
    record("missing-child", error == fnfErr, error, 0);
    error = cb_catalog_lookup(volume, 2, (ConstStr255Param)"\pEight", &entry);
    {
        CInfoPBRec pb;
        Str63 name;
        int named_file = error == noErr && !entry.directory;
        initialize_capture(&second, 0);
        memset(&pb, 0, sizeof(pb));
        name[0] = 0;
        pb.dirInfo.ioNamePtr = name;
        pb.dirInfo.ioVRefNum = volume;
        pb.dirInfo.ioDrDirID = named_file ? entry.id : 0;
        pb.dirInfo.ioFDirIndex = -1;
        /* Negative index selects DIRECTORY IDs, not arbitrary file CNIDs.
         * Compare actual native failure with propagation through the wrapper. */
        second_error = named_file ? PBGetCatInfoSync(&pb) : paramErr;
        if (named_file) error = cb_catalog_scan(volume, entry.id, 32, capture, &second);
        record("file-id-not-directory", named_file && second_error == fnfErr &&
               error == second_error && !second.count, error, second.count);
    }
    error = cb_catalog_scan(volume, 2, 32, NULL, &second);
    record("null-callback", error == paramErr, error, 0);
    query_calls = 0;
    error = cb_catalog_scan_using(volume, 2, 32, capture, &second, denied);
    record("access-error-propagated-injected", error == -5000 && query_calls == 1, error, query_calls);
    initialize_capture(&second, 0);
    query_calls = 0;
    error = cb_catalog_scan_using(volume, 2, 2, capture, &second, endless);
    record("entry-capacity-bound-injected", error == paramErr && query_calls == 3 && second.count == 2,
           error, query_calls);
    query_calls = 0;
    error = cb_catalog_scan_using(volume, 2, SHRT_MAX, capture, &second, endless);
    second_error = cb_catalog_scan_using(0, 2, 32, capture, &second, endless);
    record("index-and-volume-bounds", error == paramErr && second_error == paramErr && !query_calls,
           error, query_calls);
}
static int write_report(void)
{
    short ref, volume = 0;
    long count = (long)strlen(report), written = count;
    OSErr error;
    Str255 path = "\pUnix:morefiles-result.txt";
    error = HOpenDF(0, 0, path, fsRdWrPerm, &ref);
    if (error != noErr) return error;
    error = GetVRefNum(ref, &volume);
    if (error == noErr) error = SetEOF(ref, 0);
    if (error == noErr) error = FSWrite(ref, &written, report);
    if (written != count) error = ioErr;
    if (FSClose(ref) != noErr) error = ioErr;
    if (volume && FlushVol(NULL, volume) != noErr) error = ioErr;
    return error;
}
static void redraw(void)
{
    const char *p;
    short row = 0;
    SetPort(window); EraseRect(&window->portRect); TextFont(4); TextSize(9);
    MoveTo(8, 14);
    for (p = report; *p; ++p) {
        if (*p == '\n') MoveTo(8, (short)(14 + 13 * ++row));
        else DrawChar(*p);
    }
}
int main(void)
{
    Rect bounds = {45, 30, 445, 690};
    EventRecord event;
    int quit = 0;
    OSErr error;
    MaxApplZone(); InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus();
    TEInit(); InitDialogs(NULL); InitCursor();
    window = NewWindow(NULL, &bounds, (ConstStringPtr)"\pMoreFiles catalog probe",
                       true, documentProc, (WindowPtr)-1, true, 0);
    if (!window) return 1;
    strcpy(report, "REUSE-03 MoreFiles catalog derivative\ncommit " PROBE_COMMIT "\n");
    run_probe();
    strcat(report, all_pass ? "ALL PASS\n" : "FAILED\n");
    error = write_report();
    if (error != noErr) {
        char line[80]; snprintf(line, sizeof(line), "EVIDENCE WRITE FAILED error=%d\n", (int)error);
        strcat(report, line); all_pass = 0;
    } else strcat(report, "Evidence: Unix:morefiles-result.txt\n");
    strcat(report, "Return closes this probe after inspection.\n");
    redraw();
    while (!quit) {
        if (!WaitNextEvent(everyEvent, &event, 1, NULL)) continue;
        if (event.what == updateEvt) { BeginUpdate(window); redraw(); EndUpdate(window); }
        else if (event.what == keyDown &&
                 ((event.message & charCodeMask) == '\r' ||
                  ((event.modifiers & cmdKey) && ((event.message & charCodeMask) == 'q' ||
                   (event.message & charCodeMask) == 'Q')))) quit = 1;
        else if (event.what == mouseDown) {
            WindowPtr hit;
            short part = FindWindow(event.where, &hit);
            if (part == inGoAway && TrackGoAway(hit, event.where)) quit = 1;
            else if (part == inDrag) { Rect screen = qd.screenBits.bounds; DragWindow(hit, event.where, &screen); }
            else if (part == inContent) SelectWindow(hit);
        }
    }
    DisposeWindow(window);
    return all_pass ? 0 : 1;
}
