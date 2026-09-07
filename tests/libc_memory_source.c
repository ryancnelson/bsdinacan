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
