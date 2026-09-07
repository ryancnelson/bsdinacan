#include <stddef.h>
#include <string.h>

void *memory_source_copy(void *destination, const void *source, size_t count)
{
    return memcpy(destination, source, count);
}

void *memory_source_move(void *destination, const void *source, size_t count)
{
    return memmove(destination, source, count);
}

int memory_source_compare(const void *left, const void *right, size_t count)
{
    return memcmp(left, right, count);
}

char *memory_source_find(const char *text, int character)
{
    return strchr(text, character);
}
