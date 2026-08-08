#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timex.h>
#include <sys/types.h>

/*
 * Test-only process shim for panel_supervisord smoke verification.
 *
 * Only the two exact bhp-smoke production-shaped backlight paths are redirected. The shim also
 * returns a synchronized adjtimex observation so container clock state cannot turn a deterministic
 * daemon-loop smoke test into an unbounded startup retry. Neither behavior is linked into a target
 * binary, and neither is hardware evidence.
 */

static const char *redirect_sysfs_path(const char *path, char *redirected_path,
				       const size_t redirected_path_size)
{
	const char *fixture_leaf = NULL;
	if (strcmp(path, "/sys/class/backlight/bhp-smoke/brightness") == 0) {
		fixture_leaf = "brightness";
	} else if (strcmp(path, "/sys/class/backlight/bhp-smoke/max_brightness") == 0) {
		fixture_leaf = "max_brightness";
	} else {
		return path;
	}

	const char *fixture_root = getenv("BHP_PANEL_SUPERVISORD_SMOKE_SYSFS_ROOT");
	if (fixture_root == NULL || fixture_root[0] != '/') {
		return path;
	}
	const int path_length = snprintf(redirected_path, redirected_path_size, "%s/%s",
					 fixture_root, fixture_leaf);
	if (path_length < 0 || (size_t)path_length >= redirected_path_size) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	return redirected_path;
}

typedef int (*open_function)(const char *, int, ...);

static open_function resolve_open_function(const char *symbol_name)
{
	open_function function = NULL;
	void *symbol = dlsym(RTLD_NEXT, symbol_name);
	if (symbol == NULL || sizeof(function) != sizeof(symbol)) {
		errno = ENOSYS;
		return NULL;
	}
	memcpy(&function, &symbol, sizeof(function));
	return function;
}

static int call_redirected_open(const char *symbol_name, const char *path, const int flags,
				const mode_t mode, const int has_mode)
{
	open_function real_open = resolve_open_function(symbol_name);
	if (real_open == NULL) {
		return -1;
	}
	char redirected_path[PATH_MAX];
	const char *effective_path =
		redirect_sysfs_path(path, redirected_path, sizeof(redirected_path));
	if (effective_path == NULL) {
		return -1;
	}
	return has_mode != 0 ? real_open(effective_path, flags, mode)
			     : real_open(effective_path, flags);
}

static int open_has_mode_argument(const int flags)
{
	if ((flags & O_CREAT) != 0) {
		return 1;
	}
#ifdef O_TMPFILE
	return (flags & O_TMPFILE) == O_TMPFILE;
#else
	return 0;
#endif
}

int open(const char *path, const int flags, ...)
{
	mode_t mode = 0;
	const int has_mode = open_has_mode_argument(flags);
	if (has_mode != 0) {
		va_list arguments;
		va_start(arguments, flags);
		mode = va_arg(arguments, mode_t);
		va_end(arguments);
	}
	return call_redirected_open("open", path, flags, mode, has_mode);
}

int open64(const char *path, const int flags, ...)
{
	mode_t mode = 0;
	const int has_mode = open_has_mode_argument(flags);
	if (has_mode != 0) {
		va_list arguments;
		va_start(arguments, flags);
		mode = va_arg(arguments, mode_t);
		va_end(arguments);
	}
	return call_redirected_open("open64", path, flags, mode, has_mode);
}

int adjtimex(struct timex *clock_status)
{
	clock_status->status = 0;
	return TIME_OK;
}
