#include <string.h>
#include <sys/cdefs.h>

/* rm.c's own checkdot() calls strrchr(s, '\0') to find the string's own
   terminator (for trailing-slash trimming) before ever searching for '/'
   -- both usages are exercised here, not just the ordinary case. */
int main(int argc __unused, char *argv[] __unused)
{
    static const char sample[] = "/usr/local/bin/";
    char buffer[1];

    if (strrchr(sample, '\0') != sample + sizeof(sample) - 1)
        return 1;
    if (strrchr(sample, '/') != sample + sizeof(sample) - 2)
        return 2;
    if (strrchr(sample, 'z') != NULL)
        return 3;

    buffer[0] = '\0';
    if (strrchr(buffer, '\0') != buffer)
        return 4;
    if (strrchr(buffer, 'a') != NULL)
        return 5;

    return 0;
}
