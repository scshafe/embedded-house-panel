#include "panel_supervisor/daemon_stop_controller.hpp"

#include "panel_supervisor/error_code.hpp"

#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <string>
#include <unistd.h>

namespace house_panel::supervisor {
namespace {

volatile std::sig_atomic_t stop_requested_by_signal = 0;
volatile std::sig_atomic_t stop_channel_write_descriptor = -1;
bool stop_controller_active = false;

extern "C" void handle_daemon_stop_signal(int)
{
	const int saved_errno = errno;
	stop_requested_by_signal = 1;
	const int write_descriptor = static_cast<int>(stop_channel_write_descriptor);
	if (write_descriptor >= 0) {
		const unsigned char wake_byte = 1;
		(void)::write(write_descriptor, &wake_byte, sizeof(wake_byte));
	}
	errno = saved_errno;
}

SupervisorError stop_controller_error(const ErrorCode error_code, const std::string &message,
				      const char *operation, const int error_number)
{
	return SupervisorError(error_code, message,
			       {{"errno", std::to_string(error_number)}, {"operation", operation}},
			       error_number == EINTR || error_number == EAGAIN);
}

bool configure_stop_descriptor(const int descriptor) noexcept
{
	const int descriptor_flags = ::fcntl(descriptor, F_GETFD, 0);
	if (descriptor_flags < 0 ||
	    ::fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
		return false;
	}
	const int status_flags = ::fcntl(descriptor, F_GETFL, 0);
	return status_flags >= 0 &&
	       ::fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) >= 0;
}

void close_stop_descriptors(const int read_descriptor, const int write_descriptor) noexcept
{
	(void)::close(read_descriptor);
	(void)::close(write_descriptor);
}

} // namespace

Result<std::unique_ptr<DaemonStopController>> DaemonStopController::create()
{
	if (stop_controller_active) {
		return Result<std::unique_ptr<DaemonStopController>>::failure(SupervisorError(
			ErrorCode::DaemonStopControllerAlreadyActive,
			"Failed to create a second process-wide daemon stop controller",
			{{"operation", "daemon_stop_controller.create"}}));
	}

	int stop_descriptors[2]{-1, -1};
	if (::pipe(stop_descriptors) != 0) {
		return Result<std::unique_ptr<DaemonStopController>>::failure(
			stop_controller_error(ErrorCode::DaemonStopChannelCreateFailed,
					      "Failed to create the daemon stop self-pipe",
					      "daemon_stop_controller.pipe", errno));
	}
	if (!configure_stop_descriptor(stop_descriptors[0]) ||
	    !configure_stop_descriptor(stop_descriptors[1])) {
		const int configuration_error_number = errno;
		close_stop_descriptors(stop_descriptors[0], stop_descriptors[1]);
		return Result<std::unique_ptr<DaemonStopController>>::failure(
			stop_controller_error(
				ErrorCode::DaemonStopChannelConfigurationFailed,
				"Failed to configure nonblocking close-on-exec daemon stop descriptors",
				"daemon_stop_controller.configure_descriptors",
				configuration_error_number));
	}

	struct sigaction stop_action {};
	stop_action.sa_handler = handle_daemon_stop_signal;
	(void)sigemptyset(&stop_action.sa_mask);
	stop_action.sa_flags = 0;
	struct sigaction previous_sigint {};
	struct sigaction previous_sigterm {};
	stop_requested_by_signal = 0;
	stop_channel_write_descriptor = stop_descriptors[1];
	if (::sigaction(SIGINT, &stop_action, &previous_sigint) != 0) {
		const int signal_error_number = errno;
		stop_channel_write_descriptor = -1;
		close_stop_descriptors(stop_descriptors[0], stop_descriptors[1]);
		return Result<std::unique_ptr<DaemonStopController>>::failure(
			stop_controller_error(ErrorCode::DaemonSignalSetupFailed,
					      "Failed to install the daemon SIGINT stop handler",
					      "daemon_stop_controller.sigaction_sigint",
					      signal_error_number));
	}
	if (::sigaction(SIGTERM, &stop_action, &previous_sigterm) != 0) {
		const int signal_error_number = errno;
		(void)::sigaction(SIGINT, &previous_sigint, nullptr);
		stop_channel_write_descriptor = -1;
		close_stop_descriptors(stop_descriptors[0], stop_descriptors[1]);
		return Result<std::unique_ptr<DaemonStopController>>::failure(
			stop_controller_error(ErrorCode::DaemonSignalSetupFailed,
					      "Failed to install the daemon SIGTERM stop handler",
					      "daemon_stop_controller.sigaction_sigterm",
					      signal_error_number));
	}

	stop_controller_active = true;
	return Result<std::unique_ptr<DaemonStopController>>::success(
		std::unique_ptr<DaemonStopController>(new DaemonStopController(
			stop_descriptors[0], stop_descriptors[1], previous_sigint,
			previous_sigterm)));
}

DaemonStopController::DaemonStopController(const int read_descriptor,
					   const int write_descriptor,
					   const struct sigaction &previous_sigint,
					   const struct sigaction &previous_sigterm) noexcept
	: read_descriptor_(read_descriptor), write_descriptor_(write_descriptor),
	  previous_sigint_(previous_sigint), previous_sigterm_(previous_sigterm)
{
}

DaemonStopController::~DaemonStopController()
{
	(void)::sigaction(SIGINT, &previous_sigint_, nullptr);
	(void)::sigaction(SIGTERM, &previous_sigterm_, nullptr);
	stop_channel_write_descriptor = -1;
	stop_requested_by_signal = 0;
	stop_controller_active = false;
	close_stop_descriptors(read_descriptor_, write_descriptor_);
}

Result<bool> DaemonStopController::wait_for(const std::chrono::milliseconds duration)
{
	if (duration.count() < 0 || duration.count() > INT_MAX) {
		return Result<bool>::failure(SupervisorError(
			ErrorCode::DaemonStopWaitFailed,
			"Failed to wait for daemon stop because duration is outside poll range",
			{{"duration_milliseconds", std::to_string(duration.count())},
			 {"maximum_milliseconds", std::to_string(INT_MAX)},
			 {"operation", "daemon_stop_controller.wait_for"}}));
	}
	if (stop_requested()) {
		return Result<bool>::success(true);
	}

	pollfd stop_poll_descriptor{read_descriptor_, POLLIN, 0};
	int poll_result = 0;
	do {
		poll_result = ::poll(&stop_poll_descriptor, 1, static_cast<int>(duration.count()));
	} while (poll_result < 0 && errno == EINTR && !stop_requested());
	if (stop_requested()) {
		return Result<bool>::success(true);
	}
	if (poll_result < 0) {
		return Result<bool>::failure(stop_controller_error(
			ErrorCode::DaemonStopWaitFailed,
			"Failed while polling the daemon stop self-pipe",
			"daemon_stop_controller.poll", errno));
	}
	if (poll_result == 0) {
		return Result<bool>::success(false);
	}
	if ((stop_poll_descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
		return Result<bool>::failure(SupervisorError(
			ErrorCode::DaemonStopWaitFailed,
			"Failed because the daemon stop self-pipe reported an invalid poll state",
			{{"operation", "daemon_stop_controller.poll_revents"},
			 {"poll_revents", std::to_string(stop_poll_descriptor.revents)}}));
	}

	unsigned char wake_bytes[32]{};
	while (true) {
		const ssize_t read_result = ::read(read_descriptor_, wake_bytes, sizeof(wake_bytes));
		if (read_result > 0) {
			continue;
		}
		if (read_result < 0 && errno == EINTR) {
			continue;
		}
		if (read_result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			break;
		}
		if (read_result == 0) {
			break;
		}
		return Result<bool>::failure(stop_controller_error(
			ErrorCode::DaemonStopWaitFailed,
			"Failed to drain the daemon stop self-pipe",
			"daemon_stop_controller.read", errno));
	}
	return Result<bool>::success(stop_requested());
}

bool DaemonStopController::stop_requested() const noexcept
{
	return stop_requested_by_signal != 0;
}

} // namespace house_panel::supervisor
