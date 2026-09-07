#include <stddef.h>
#include <stdlib.h>

void *allocation_source_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

void *allocation_source_realloc(void *pointer, size_t size)
{
    return realloc(pointer, size);
}
