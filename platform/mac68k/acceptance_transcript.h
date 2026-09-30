#ifndef CB_MAC_ACCEPTANCE_TRANSCRIPT_H
#define CB_MAC_ACCEPTANCE_TRANSCRIPT_H

/* The complete success transcript bounds every failure transcript too:
   FAIL and PASS have equal lengths, failure stops the cases early, and
   FAILED is shorter than ALL PASS. sizeof includes the terminating NUL.
   This is a C99 integer constant expression on both 32- and 64-bit hosts;
   adding a shared case automatically grows the guest's result buffer. */
#define CB_MAC_RESULT_HEADER "cannedBSD System 7 / Retro68\n"
enum {
    CB_MAC_RESULT_CAPACITY = sizeof(CB_MAC_RESULT_HEADER
        "PASS contexts\n"
        "PASS terminalengine\n"
        "PASS consolewrite\n"
        "PASS teestate\n"
        "PASS interrupts\n"
#define CB_MAC_CASE(command, expected, status) "PASS " command "\n"
#include "acceptance_cases.def"
#undef CB_MAC_CASE
        "ALL PASS\n")
};

#endif
