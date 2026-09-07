#include <stddef.h>
#include <unistd.h>

const char *environ_source_first(void)
{
    return environ != NULL ? environ[0] : NULL;
}

void environ_source_replace(char **vector)
{
    environ = vector;
}
