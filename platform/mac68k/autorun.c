#include "autorun.h"

int cb_mac_finish_autorun(int passed, const char *result,
                         const struct cb_mac_autorun_ops *ops)
{
    if (ops->write_result(result) < 0) return -1;
    if (ops->capture_screen() < 0) return -1;
    return ops->write_done(passed ? "PASS\n" : "FAIL\n");
}
