#include <dirent.h>
#include <errno.h>

static DIR *shared_dir;

int dirent_rebind_open(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	shared_dir = opendir("/");
	return shared_dir == NULL;
}

int dirent_rebind_readdir(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	return readdir(shared_dir) == NULL;
}

int dirent_rebind_closedir_reject(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	errno = 0;
	if (closedir(shared_dir) != -1)
		return 1;
	if (errno != ENOSYS)
		return 2;
	return 0;
}

int dirent_rebind_closedir_accept(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	return closedir(shared_dir) != 0;
}
