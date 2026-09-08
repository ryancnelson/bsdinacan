#include <stdlib.h>
#include <string.h>

/* Like head's obsolete-option rewrite: leave the malloc-owned replacement in
   the public vector. Neither free it nor restore the original entry. */
int main(int argc, char **argv)
{
    char *replacement;
    if (argc < 2) return 1;
    replacement = malloc(32);
    if (replacement == NULL) return 2;
    memcpy(replacement, "replacement argument", 21);
    argv[1] = replacement;
    return 0;
}
