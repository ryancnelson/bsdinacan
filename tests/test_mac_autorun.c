#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "autorun.h"

static unsigned step, fail_at;
static const char *expected_done;
static int result(const char *text)
{
    assert(++step == 1);
    assert(strcmp(text, "actual test output") == 0);
    return fail_at == step ? -1 : 0;
}
static int screenshot(void)
{
    assert(++step == 2); /* Result must be durable before picture capture. */
    return fail_at == step ? -1 : 0;
}
static int done(const char *text)
{
    assert(++step == 3); /* Neither evidence write may be skipped. */
    assert(strcmp(text, expected_done) == 0);
    return fail_at == step ? -1 : 0;
}
static const struct cb_mac_autorun_ops ops = {result, screenshot, done};

int main(void)
{
    unsigned failed;
    expected_done = "PASS\n";
    assert(cb_mac_finish_autorun(1, "actual test output", &ops) == 0);
    assert(step == 3);
    step = 0;
    expected_done = "FAIL\n";
    assert(cb_mac_finish_autorun(0, "actual test output", &ops) == 0);
    assert(step == 3); /* A failed test still requires honest complete evidence. */
    expected_done = "PASS\n";
    for (failed = 1; failed <= 3; ++failed) {
        step = 0;
        fail_at = failed;
        assert(cb_mac_finish_autorun(1, "actual test output", &ops) < 0);
        assert(step == failed); /* No completion after partial evidence. */
    }
    puts("Mac autorun orchestration tests passed");
    return 0;
}
