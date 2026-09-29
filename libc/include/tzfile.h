#ifndef CANNEDBSD_TZFILE_H
#define CANNEDBSD_TZFILE_H

/*
 * LS-02: pinned ls/print.c's printtime() only reaches into this header
 * for two arithmetic constants (SIXMONTHS = (DAYSPERNYEAR / 2) *
 * SECSPERDAY); the real NetBSD tzfile.h additionally defines the on-disk
 * TZif format and leap-second tables, none of which any pinned consumer
 * in this tree uses, so only the two constants are provided here.
 */
#define DAYSPERNYEAR 365
#define SECSPERDAY   (24 * 60 * 60)

#endif
