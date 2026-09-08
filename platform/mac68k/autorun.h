#ifndef CB_MAC_AUTORUN_H
#define CB_MAC_AUTORUN_H

struct cb_mac_autorun_ops {
    int (*write_result)(const char *);
    int (*capture_screen)(void);
    int (*write_done)(const char *);
};
/* Each operation returns success only after closing and flushing its file. */
int cb_mac_finish_autorun(int passed, const char *result,
                         const struct cb_mac_autorun_ops *ops);

#endif
