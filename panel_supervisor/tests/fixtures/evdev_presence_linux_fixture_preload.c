#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Test-only Linux ioctl shim for the production EvdevPresenceSource.
 *
 * The shim answers only for the exact regular-file device path supplied by the verification
 * harness. It records every accepted request before exposing SW_FRONT_PROXIMITY capability or
 * current state. It is never linked into an application or target image and is not hardware
 * evidence.
 */

#define BHP_BITS_PER_WORD (sizeof(unsigned long) * CHAR_BIT)
#define BHP_SWITCH_WORD_COUNT ((SW_MAX + BHP_BITS_PER_WORD) / BHP_BITS_PER_WORD)
#define BHP_SWITCH_BYTES (sizeof(unsigned long) * BHP_SWITCH_WORD_COUNT)

typedef int (*ioctl_function)(int, unsigned long, ...);

static ioctl_function resolve_real_ioctl(void)
{
	ioctl_function function = NULL;
	void *symbol = dlsym(RTLD_NEXT, "ioctl");
	if (symbol == NULL || sizeof(function) != sizeof(symbol)) {
		errno = ENOSYS;
		return NULL;
	}
	memcpy(&function, &symbol, sizeof(function));
	return function;
}

static int descriptor_matches_fixture(const int descriptor)
{
	const char *expected_path = getenv("BHP_EVDEV_PRESENCE_LINUX_DEVICE_PATH");
	if (expected_path == NULL || expected_path[0] != '/') {
		return 0;
	}
	char descriptor_link_path[64];
	const int link_path_length = snprintf(descriptor_link_path, sizeof(descriptor_link_path),
					      "/proc/self/fd/%d", descriptor);
	if (link_path_length < 0 || (size_t)link_path_length >= sizeof(descriptor_link_path)) {
		return 0;
	}
	char resolved_path[PATH_MAX];
	const ssize_t resolved_length =
		readlink(descriptor_link_path, resolved_path, sizeof(resolved_path) - 1);
	if (resolved_length < 0 || (size_t)resolved_length >= sizeof(resolved_path)) {
		return 0;
	}
	resolved_path[resolved_length] = '\0';
	return strcmp(resolved_path, expected_path) == 0;
}

static int write_request_record(const char *request_name, const unsigned long request)
{
	const char *request_log_path = getenv("BHP_EVDEV_PRESENCE_LINUX_REQUEST_LOG");
	if (request_log_path == NULL || request_log_path[0] != '/') {
		errno = EINVAL;
		return -1;
	}
	int flags = O_WRONLY | O_APPEND | O_CREAT;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	const int descriptor = open(request_log_path, flags, 0600);
	if (descriptor < 0) {
		return -1;
	}
	char record[96];
	const int record_length =
		snprintf(record, sizeof(record), "%s=%lu\n", request_name, request);
	if (record_length < 0 || (size_t)record_length >= sizeof(record) ||
	    write(descriptor, record, (size_t)record_length) != record_length) {
		const int write_error = errno == 0 ? EIO : errno;
		(void)close(descriptor);
		errno = write_error;
		return -1;
	}
	return close(descriptor);
}

static int read_asserted_state(void)
{
	const char *state_path = getenv("BHP_EVDEV_PRESENCE_LINUX_STATE_PATH");
	if (state_path == NULL || state_path[0] != '/') {
		errno = EINVAL;
		return -1;
	}
	int flags = O_RDONLY;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	const int descriptor = open(state_path, flags);
	if (descriptor < 0) {
		return -1;
	}
	char state[2] = {0, 0};
	const ssize_t state_length = read(descriptor, state, sizeof(state));
	const int read_error = errno;
	if (close(descriptor) != 0) {
		return -1;
	}
	if (state_length != 1 || (state[0] != '0' && state[0] != '1')) {
		errno = state_length < 0 ? read_error : EINVAL;
		return -1;
	}
	return state[0] == '1';
}

static void set_switch_bit(unsigned long *words, const unsigned int bit)
{
	words[bit / BHP_BITS_PER_WORD] |= 1UL << (bit % BHP_BITS_PER_WORD);
}

int ioctl(const int descriptor, const unsigned long request, ...)
{
	va_list arguments;
	va_start(arguments, request);
	void *argument = va_arg(arguments, void *);
	va_end(arguments);

	if (!descriptor_matches_fixture(descriptor)) {
		ioctl_function real_ioctl = resolve_real_ioctl();
		return real_ioctl == NULL ? -1 : real_ioctl(descriptor, request, argument);
	}

	const unsigned long capability_request = EVIOCGBIT(EV_SW, BHP_SWITCH_BYTES);
	const unsigned long state_request = EVIOCGSW(BHP_SWITCH_BYTES);
	if (request == capability_request) {
		if (write_request_record("EVIOCGBIT_EV_SW", request) != 0) {
			return -1;
		}
		memset(argument, 0, BHP_SWITCH_BYTES);
		set_switch_bit((unsigned long *)argument, SW_FRONT_PROXIMITY);
		return (int)BHP_SWITCH_BYTES;
	}
	if (request == state_request) {
		if (write_request_record("EVIOCGSW", request) != 0) {
			return -1;
		}
		const int asserted = read_asserted_state();
		if (asserted < 0) {
			return -1;
		}
		memset(argument, 0, BHP_SWITCH_BYTES);
		if (asserted != 0) {
			set_switch_bit((unsigned long *)argument, SW_FRONT_PROXIMITY);
		}
		return (int)BHP_SWITCH_BYTES;
	}

	errno = ENOTTY;
	return -1;
}
