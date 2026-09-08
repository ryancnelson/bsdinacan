#include <dirent.h>
#include <errno.h>

int main(int argc, char *argv[])
{
	DIR *dirp;
	(void)argc;
	(void)argv;
	dirp = opendir("/");
	if (dirp == NULL)
		return 2;
	errno = 0;
	if (readdir(dirp) != NULL)
		return 3;
	if (errno != ENOSYS)
		return 4;
	if (closedir(dirp) != 0)
		return 5;
	return 0;
}
