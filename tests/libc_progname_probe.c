#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    const char *expected = argc > 1 ? argv[1] : "libcprognameprobe";
    char *original = argv[0];
    const char *component = original;
    const char *cursor;
    const char *name;
    int preserved;

    for (cursor = original; *cursor != '\0'; ++cursor)
        if (*cursor == '/')
            component = cursor + 1;
    name = getprogname();
    if (name != component || strcmp(name, expected) != 0)
        return 1;
    setprogname("different/ignored-name");
    if (getprogname() != name || strcmp(name, expected) != 0)
        return 2;
    argv[0] = (char *)"replacement/vector-slot";
    preserved = getprogname() == name && strcmp(name, expected) == 0;
    argv[0] = original;
    if (!preserved)
        return 3;
    return 0;
}
