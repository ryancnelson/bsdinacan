#include <libgen.h>
#include <errno.h>

int main(int argc, char *argv[])
{
	char path[] = "/a/b";
	char *result;
	(void)argc;
	(void)argv;
	errno = 0;
	result = dirname(path);
	if (result != NULL)
		return 2;
	if (errno != ENOSYS)
		return 3;
	return 0;
}
