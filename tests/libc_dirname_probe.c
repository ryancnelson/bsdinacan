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
	/* A dirname component exactly 1023 bytes long -- the maximum any
	   PATH_MAX == CB_PATH_MAX == 1024 buffer can hold alongside its NUL.
	   Upstream's own xdirname_r clamps to exactly this length
	   (buflen - 1); this proves the veneer's copy-out reproduces that
	   exact-fit result without an off-by-one truncating it any further. */
	for (index = 0; index < 1023; ++index)
		buffer[index] = 'a';
	buffer[1023] = '/';
	buffer[1024] = 'x';
	buffer[1025] = '\0';
	result = dirname(buffer);
	return result != NULL && strlen(result) == 1023 && result[0] == 'a' &&
	       result[1022] == 'a';
}

static int check_overflow(void)
{
	static char buffer[1200];
	char *result;
	size_t index;
	size_t length;
	/* A dirname component of 1100 bytes -- longer than PATH_MAX can hold.
	   Upstream's own xdirname_r truncates its result to buflen - 1 (1023)
	   bytes before this veneer ever sees it; this proves the veneer
	   faithfully reproduces that already-truncated result (exactly 1023
	   bytes, still all 'a') rather than reading or copying past it. */
	for (index = 0; index < 1100; ++index)
		buffer[index] = 'a';
	buffer[1100] = '/';
	buffer[1101] = 'x';
	buffer[1102] = '\0';
	result = dirname(buffer);
	if (result == NULL)
		return 0;
	length = strlen(result);
	if (length != 1023)
		return 0;
	for (index = 0; index < length; ++index) {
		if (result[index] != 'a')
			return 0;
	}
	return 1;
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
	if (!check_overflow())
		return 11;
	if (!check_repeated_calls())
		return 12;
	return 0;
}
