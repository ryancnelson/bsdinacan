#include <dirent.h>
#include <errno.h>

int main(int argc, char *argv[])
{
	DIR *dirp;
	(void)argc;
	(void)argv;
	errno = 0;
	dirp = opendir("/");
	if (dirp != NULL) {
		closedir(dirp);
		return 2;
	}
	if (errno != ENOMEM)
		return 3;
	return 0;
}
