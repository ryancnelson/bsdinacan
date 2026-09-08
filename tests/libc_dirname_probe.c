#include <libgen.h>
#include <string.h>

static int check(const char *input, const char *expected)
{
	char buffer[64];
	char *result;
	if (input == NULL) {
		result = dirname(NULL);
	} else {
		size_t length = strlen(input);
		size_t index;
		for (index = 0; index <= length; ++index)
			buffer[index] = input[index];
		result = dirname(buffer);
	}
	return result != NULL && strcmp(result, expected) == 0;
}

static int check_boundary(void)
{
	static char buffer[1030];
	char *result;
	size_t index;
	/* A dirname component exactly CB_PATH_MAX - 2 bytes long: with the
	   "/x" suffix and NUL, the full result is CB_PATH_MAX - 1 bytes plus
	   a terminator -- fits exactly in the CB_PATH_MAX-sized task buffer,
	   proving the veneer's copy-out does not truncate anything upstream
	   itself would not already have truncated. */
	for (index = 0; index < 1022; ++index)
		buffer[index] = 'a';
	buffer[1022] = '/';
	buffer[1023] = 'x';
	buffer[1024] = '\0';
	result = dirname(buffer);
	return result != NULL && strlen(result) == 1022 && result[0] == 'a' &&
	       result[1021] == 'a';
}

static int check_repeated_calls(void)
{
	char first[] = "/first/path";
	char second[] = "/second/different/path";
	char *result;

	result = dirname(first);
	if (result == NULL || strcmp(result, "/first") != 0)
		return 0;
	/* Same task, second call: must observe the SECOND call's result, not
	   the first -- matches upstream's own single-buffer-per-call
	   convention (this is not testing cross-task isolation, only that
	   the veneer does not accidentally cache or freeze the first
	   result). */
	result = dirname(second);
	return result != NULL && strcmp(result, "/second/different") == 0;
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	if (!check(NULL, "."))
		return 2;
	if (!check("", "."))
		return 3;
	if (!check("foo", "."))
		return 4;
	if (!check("/", "/"))
		return 5;
	if (!check("////", "/"))
		return 6;
	if (!check("/foo/bar", "/foo"))
		return 7;
	if (!check("/foo/bar///", "/foo"))
		return 8;
	if (!check("foo/bar", "foo"))
		return 9;
	if (!check_boundary())
		return 10;
	if (!check_repeated_calls())
		return 11;
	return 0;
}
