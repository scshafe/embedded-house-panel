#include "panel_supervisor/capture_closure.hpp"

#include "panel_supervisor/supervisor_error.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/syscall.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace house_panel::supervisor {
namespace {

constexpr auto kMaximumClosureDeadline = std::chrono::seconds(30);

bool all_digits(const std::string &text, const std::size_t begin = 0,
		std::size_t end = std::string::npos)
{
	end = std::min(end, text.size());
	if (begin >= end) {
		return false;
	}
	return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(begin),
			   text.begin() + static_cast<std::ptrdiff_t>(end),
			   [](const char c) { return c >= '0' && c <= '9'; });
}

/** `<prefix><digits><suffix>` with at least one digit. */
bool prefix_digits_suffix(const std::string &name, const std::string &prefix,
			  const std::string &suffix = {})
{
	return name.size() > prefix.size() + suffix.size() &&
	       name.compare(0, prefix.size(), prefix) == 0 &&
	       name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0 &&
	       all_digits(name, prefix.size(), name.size() - suffix.size());
}

/** `/dev/snd/pcmC<card>D<device>c`, optionally followed by ` (deleted)`. */
bool is_capture_node(std::string target)
{
	static const std::string deleted = " (deleted)";
	if (target.size() > deleted.size() &&
	    target.compare(target.size() - deleted.size(), deleted.size(), deleted) == 0) {
		target.resize(target.size() - deleted.size());
	}
	static const std::string prefix = "/dev/snd/pcmC";
	if (target.size() <= prefix.size() + 3 || target.compare(0, prefix.size(), prefix) != 0 ||
	    target.back() != 'c') {
		return false;
	}
	const std::size_t device_marker = target.find('D', prefix.size());
	return device_marker != std::string::npos &&
	       all_digits(target, prefix.size(), device_marker) &&
	       all_digits(target, device_marker + 1, target.size() - 1);
}

/** Parses a positive process ID; anything else is not a process directory. */
bool parse_pid(const std::string &name, int &pid)
{
	if (!all_digits(name) || name.size() > 10) {
		return false;
	}
	const long parsed = std::strtol(name.c_str(), nullptr, 10);
	if (parsed <= 0 || parsed > INT_MAX) {
		return false;
	}
	pid = static_cast<int>(parsed);
	return true;
}

SupervisorError probe_error(const std::string &message, const std::string &operation,
			    const int error_number, const std::string &detail)
{
	return SupervisorError(ErrorCode::CaptureClosureProbeFailed, message,
			       {{"detail", detail},
				{"errno", std::to_string(error_number)},
				{"operation", "capture_closure.observe." + operation}});
}

enum class DirectoryState { Present, Missing, Hidden };

/**
 * Lists entry names in ascending order. A vanished directory (process exit, card removal, or
 * no ALSA) is `Missing`; a kernel-refused one (EACCES/EPERM) is `Hidden`. Both return an empty
 * list, and each caller decides whether that state is acceptable. Every other failure is an
 * error.
 */
Result<std::vector<std::string>> list_directory(const std::string &path, const std::string &detail,
						DirectoryState &state)
{
	const auto classify = [&state](const int error_number) {
		if (error_number == ENOENT || error_number == ESRCH) {
			state = DirectoryState::Missing;
			return true;
		}
		if (error_number == EACCES || error_number == EPERM) {
			state = DirectoryState::Hidden;
			return true;
		}
		return false;
	};
	state = DirectoryState::Present;
	DIR *directory = ::opendir(path.c_str());
	if (directory == nullptr) {
		const int open_errno = errno;
		if (classify(open_errno)) {
			return Result<std::vector<std::string>>::success({});
		}
		return Result<std::vector<std::string>>::failure(probe_error(
			"Failed to open capture evidence directory", "opendir", open_errno, detail));
	}
	std::vector<std::string> names;
	int read_errno = 0;
	while (true) {
		errno = 0;
		const dirent *entry = ::readdir(directory);
		if (entry == nullptr) {
			read_errno = errno;
			break;
		}
		const std::string name = entry->d_name;
		if (name != "." && name != "..") {
			names.push_back(name);
		}
	}
	::closedir(directory);
	if (read_errno != 0) {
		if (classify(read_errno)) {
			return Result<std::vector<std::string>>::success({});
		}
		return Result<std::vector<std::string>>::failure(probe_error(
			"Failed to read capture evidence directory", "readdir", read_errno, detail));
	}
	std::sort(names.begin(), names.end());
	return Result<std::vector<std::string>>::success(std::move(names));
}

SupervisorError hidden_evidence_error(const std::string &detail)
{
	return probe_error("Capture evidence directory is hidden from the supervisor", "opendir",
			   EACCES, detail);
}

/** Reads at most 4 KiB of a procfs text file; sets `error_number` on failure. */
bool read_small_file(const std::string &path, std::string &content, int &error_number)
{
	const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	if (descriptor < 0) {
		error_number = errno;
		return false;
	}
	content.clear();
	char buffer[4096];
	while (content.size() < sizeof(buffer)) {
		const ssize_t count = ::read(descriptor, buffer, sizeof(buffer));
		if (count < 0) {
			if (errno == EINTR) {
				continue;
			}
			error_number = errno;
			::close(descriptor);
			return false;
		}
		if (count == 0) {
			break;
		}
		content.append(buffer, static_cast<std::size_t>(count));
	}
	::close(descriptor);
	return true;
}

int open_pidfd(const int pid)
{
#ifdef SYS_pidfd_open
	return static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
#else
	(void)pid;
	errno = ENOSYS;
	return -1;
#endif
}

int send_pidfd_signal(const int pidfd, const int signal_number)
{
#ifdef SYS_pidfd_send_signal
	return static_cast<int>(::syscall(SYS_pidfd_send_signal, pidfd, signal_number, nullptr, 0));
#else
	(void)pidfd;
	(void)signal_number;
	errno = ENOSYS;
	return -1;
#endif
}

class PidfdGuard {
public:
	explicit PidfdGuard(const int descriptor) : descriptor_(descriptor) {}
	~PidfdGuard()
	{
		if (descriptor_ >= 0) {
			::close(descriptor_);
		}
	}
	PidfdGuard(const PidfdGuard &) = delete;
	PidfdGuard &operator=(const PidfdGuard &) = delete;

private:
	int descriptor_;
};

enum class Ownership { Owned, NotOwned, Exited };

/** Both the real and the effective UID must equal `uid`. */
Ownership read_ownership(const std::string &status_path, const std::uint32_t uid)
{
	std::ifstream status(status_path);
	if (!status) {
		return Ownership::Exited;
	}
	std::string line;
	while (std::getline(status, line)) {
		if (line.rfind("Uid:", 0) != 0) {
			continue;
		}
		std::istringstream fields(line.substr(4));
		unsigned long real_uid = 0;
		unsigned long effective_uid = 0;
		if (!(fields >> real_uid >> effective_uid)) {
			return Ownership::NotOwned;
		}
		return real_uid == uid && effective_uid == uid ? Ownership::Owned
							       : Ownership::NotOwned;
	}
	return Ownership::NotOwned;
}

SupervisorError termination_error(const std::string &message, const int error_number,
				  const bool force, const std::string &operation)
{
	return SupervisorError(ErrorCode::MediaTerminationFailed, message,
			       {{"errno", std::to_string(error_number)},
				{"forced", force ? "true" : "false"},
				{"operation", "media_terminator.terminate." + operation}});
}

} // namespace

ProcfsCaptureClosureProbe::ProcfsCaptureClosureProbe(std::string proc_root)
    : proc_root_(std::move(proc_root))
{
}

Result<CaptureClosureObservation> ProcfsCaptureClosureProbe::observe()
{
	CaptureClosureObservation observation;
	const std::string asound = proc_root_ + "/asound";
	DirectoryState state = DirectoryState::Present;
	auto cards = list_directory(asound, "asound", state);
	if (!cards) {
		return Result<CaptureClosureObservation>::failure(cards.error());
	}
	if (state == DirectoryState::Hidden) {
		return Result<CaptureClosureObservation>::failure(hidden_evidence_error("asound"));
	}
	for (const auto &card : cards.value()) {
		if (!prefix_digits_suffix(card, "card")) {
			continue; // Also skips the card-ID symlinks, which alias cardN.
		}
		auto pcms = list_directory(asound + "/" + card, card, state);
		if (!pcms) {
			return Result<CaptureClosureObservation>::failure(pcms.error());
		}
		if (state == DirectoryState::Hidden) {
			return Result<CaptureClosureObservation>::failure(hidden_evidence_error(card));
		}
		for (const auto &pcm : pcms.value()) {
			if (!prefix_digits_suffix(pcm, "pcm", "c")) {
				continue;
			}
			const std::string pcm_path = asound + "/" + card + "/" + pcm;
			auto substreams = list_directory(pcm_path, card + "/" + pcm, state);
			if (!substreams) {
				return Result<CaptureClosureObservation>::failure(substreams.error());
			}
			if (state == DirectoryState::Hidden) {
				return Result<CaptureClosureObservation>::failure(
					hidden_evidence_error(card + "/" + pcm));
			}
			for (const auto &sub : substreams.value()) {
				if (!prefix_digits_suffix(sub, "sub")) {
					continue;
				}
				const std::string name = card + "/" + pcm + "/" + sub;
				std::string text;
				int status_errno = 0;
				if (!read_small_file(pcm_path + "/" + sub + "/status", text,
						     status_errno)) {
					if (status_errno == ENOENT) {
						continue; // The card was removed during the scan.
					}
					return Result<CaptureClosureObservation>::failure(
						probe_error("Capture substream status is unreadable",
							    "status", status_errno, name));
				}
				while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
					text.pop_back();
				}
				if (text != "closed") {
					observation.open_substreams.push_back(name);
				}
			}
		}
	}

	auto processes = list_directory(proc_root_, "proc", state);
	if (!processes) {
		return Result<CaptureClosureObservation>::failure(processes.error());
	}
	if (state != DirectoryState::Present) {
		// Without a process table no holder evidence exists; that is never "closed".
		return Result<CaptureClosureObservation>::failure(
			probe_error("Process table is missing or hidden", "opendir", ENOENT, "proc"));
	}
	for (const auto &process : processes.value()) {
		int pid = 0;
		if (!parse_pid(process, pid)) {
			continue;
		}
		const std::string fd_directory = proc_root_ + "/" + process + "/fd";
		auto descriptors = list_directory(fd_directory, process + "/fd", state);
		if (!descriptors) {
			return Result<CaptureClosureObservation>::failure(descriptors.error());
		}
		bool holds_capture = false;
		bool hidden = state == DirectoryState::Hidden;
		for (const auto &descriptor : descriptors.value()) {
			char target[PATH_MAX];
			const std::string link = fd_directory + "/" + descriptor;
			const ssize_t length = ::readlink(link.c_str(), target, sizeof(target) - 1);
			if (length < 0) {
				const int link_errno = errno;
				if (link_errno == ENOENT || link_errno == ESRCH) {
					continue; // Closed or exited during the scan.
				}
				if (link_errno == EACCES || link_errno == EPERM) {
					hidden = true;
					break;
				}
				return Result<CaptureClosureObservation>::failure(
					probe_error("Process descriptor link is unreadable",
						    "readlink", link_errno, process + "/fd/" + descriptor));
			}
			target[length] = '\0';
			if (is_capture_node(target)) {
				holds_capture = true;
				break;
			}
		}
		if (holds_capture) {
			observation.holder_pids.push_back(pid);
		} else if (hidden) {
			++observation.unreadable_processes;
		}
	}
	std::sort(observation.holder_pids.begin(), observation.holder_pids.end());
	return Result<CaptureClosureObservation>::success(std::move(observation));
}

UidScopedMediaTerminator::UidScopedMediaTerminator(const std::uint32_t broker_uid,
						   std::string proc_root)
    : broker_uid_(broker_uid), proc_root_(std::move(proc_root))
{
}

Result<void> UidScopedMediaTerminator::terminate(const std::vector<int> &holder_pids,
						 const bool force)
{
	if (broker_uid_ == 0) {
		return Result<void>::failure(SupervisorError(
			ErrorCode::MediaTerminationFailed,
			"Media termination never targets root-owned holders",
			{{"forced", force ? "true" : "false"},
			 {"operation", "media_terminator.terminate"}}));
	}
	for (const int pid : holder_pids) {
		if (pid <= 0) {
			continue;
		}
		const int pidfd = open_pidfd(pid);
		if (pidfd < 0) {
			const int open_errno = errno;
			if (open_errno == ESRCH) {
				continue; // Exited since the probe: nothing to signal.
			}
			return Result<void>::failure(termination_error(
				"Failed to pin a capture holder process", open_errno, force,
				"pidfd_open"));
		}
		PidfdGuard guard(pidfd);
		// The pidfd holds the PID, so this status belongs to the process it signals.
		const Ownership ownership =
			read_ownership(proc_root_ + "/" + std::to_string(pid) + "/status", broker_uid_);
		if (ownership != Ownership::Owned) {
			continue; // Exited, or not the broker: leave it, so closure stays unverified.
		}
		if (send_pidfd_signal(pidfd, force ? SIGKILL : SIGTERM) != 0) {
			const int signal_errno = errno;
			if (signal_errno == ESRCH) {
				continue;
			}
			return Result<void>::failure(termination_error(
				"Failed to signal the media broker", signal_errno, force,
				"pidfd_send_signal"));
		}
	}
	return Result<void>::success();
}

Result<CaptureClosureBounds> validate_capture_closure_bounds(const CaptureClosureBounds bounds)
{
	if (bounds.poll_interval.count() <= 0 || bounds.termination_grace.count() <= 0 ||
	    bounds.closure_deadline <= bounds.termination_grace ||
	    bounds.poll_interval > bounds.termination_grace ||
	    bounds.closure_deadline > kMaximumClosureDeadline) {
		return Result<CaptureClosureBounds>::failure(SupervisorError(
			ErrorCode::SuspendConfigurationInvalid,
			"Capture closure bounds must be positive, ordered and at most 30 seconds",
			{{"closure_deadline_milliseconds", std::to_string(bounds.closure_deadline.count())},
			 {"operation", "validate_capture_closure_bounds"},
			 {"poll_interval_milliseconds", std::to_string(bounds.poll_interval.count())},
			 {"termination_grace_milliseconds",
			  std::to_string(bounds.termination_grace.count())}}));
	}
	return Result<CaptureClosureBounds>::success(bounds);
}

CaptureClosureGatedSuspend::CaptureClosureGatedSuspend(Suspend &configured_suspend,
						       CaptureClosureProbe &probe,
						       MediaTerminator *terminator,
						       const CaptureClosureBounds bounds,
						       Sleeper sleeper, Clock clock)
    : configured_suspend_(configured_suspend), probe_(probe), terminator_(terminator),
      bounds_(bounds),
      sleeper_(sleeper ? std::move(sleeper)
		       : Sleeper([](const std::chrono::milliseconds duration) {
				 std::this_thread::sleep_for(duration);
			 })),
      clock_(clock ? std::move(clock) : Clock([] { return std::chrono::steady_clock::now(); }))
{
}

Result<CaptureClosureObservation>
CaptureClosureGatedSuspend::wait_for_closure(const std::chrono::steady_clock::time_point until)
{
	while (true) {
		auto observation = probe_.observe();
		if (!observation || observation.value().closed() || clock_() >= until) {
			return observation;
		}
		sleeper_(bounds_.poll_interval);
	}
}

Result<void> CaptureClosureGatedSuspend::request_suspend()
{
	last_report_ = CaptureClosureReport{};
	const auto started = clock_();
	const auto elapsed = [this, started] {
		return std::chrono::duration_cast<std::chrono::milliseconds>(clock_() - started);
	};
	auto initial = probe_.observe();
	if (!initial) {
		return Result<void>::failure(initial.error());
	}
	last_report_.initial_open_substreams = initial.value().open_substreams.size();
	last_report_.initial_holders = initial.value().holder_pids.size();
	last_report_.initial_unreadable_processes = initial.value().unreadable_processes;
	const auto refuse = [this, &elapsed](const ErrorCode code, const char *message,
					     const CaptureClosureObservation &observation) {
		last_report_.waited = elapsed();
		return Result<void>::failure(SupervisorError(
			code, message,
			{{"final_holders", std::to_string(observation.holder_pids.size())},
			 {"final_open_substreams", std::to_string(observation.open_substreams.size())},
			 {"forced", last_report_.forced ? "true" : "false"},
			 {"initial_holders", std::to_string(last_report_.initial_holders)},
			 {"initial_unreadable_processes",
			  std::to_string(last_report_.initial_unreadable_processes)},
			 {"initial_open_substreams",
			  std::to_string(last_report_.initial_open_substreams)},
			 {"operation", "capture_closure_gated_suspend.request_suspend"},
			 {"waited_milliseconds", std::to_string(last_report_.waited.count())}},
			true));
	};
	if (!initial.value().closed()) {
		if (terminator_ == nullptr) {
			return refuse(ErrorCode::CaptureTerminationUnavailable,
				      "Refused suspend: microphone capture is open and no bounded "
				      "termination path is configured",
				      initial.value());
		}
		last_report_.termination_requested = true;
		auto signalled = terminator_->terminate(initial.value().holder_pids, false);
		if (!signalled) {
			last_report_.waited = elapsed();
			return Result<void>::failure(signalled.error());
		}
		auto after_grace = wait_for_closure(started + bounds_.termination_grace);
		if (!after_grace) {
			last_report_.waited = elapsed();
			return Result<void>::failure(after_grace.error());
		}
		if (!after_grace.value().closed()) {
			last_report_.forced = true;
			auto killed = terminator_->terminate(after_grace.value().holder_pids, true);
			if (!killed) {
				last_report_.waited = elapsed();
				return Result<void>::failure(killed.error());
			}
			auto final_observation = wait_for_closure(started + bounds_.closure_deadline);
			if (!final_observation) {
				last_report_.waited = elapsed();
				return Result<void>::failure(final_observation.error());
			}
			if (!final_observation.value().closed()) {
				return refuse(ErrorCode::CaptureClosureUnverified,
					      "Refused suspend: kernel-visible microphone capture is "
					      "still open after the bounded termination path",
					      final_observation.value());
			}
		}
	}
	last_report_.verified = true;
	last_report_.waited = elapsed();
	return configured_suspend_.request_suspend();
}

} // namespace house_panel::supervisor
