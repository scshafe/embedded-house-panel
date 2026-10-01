#include "panel_supervisor/capture_closure.hpp"

#include "panel_supervisor/supervisor_error.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace house_panel::supervisor {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                       \
		if (!(condition)) {                                                                \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';      \
			return false;                                                              \
		}                                                                                  \
	} while (false)

namespace fs = std::filesystem;

/** A disposable fake procfs tree; removed on destruction. */
class FixtureProc {
public:
	FixtureProc()
	{
		char pattern[] = "/tmp/bhp-capture-closure-XXXXXX";
		const char *created = ::mkdtemp(pattern);
		root_ = created != nullptr ? created : "";
	}
	~FixtureProc()
	{
		std::error_code ignored;
		if (!root_.empty()) {
			fs::remove_all(root_, ignored);
		}
	}
	FixtureProc(const FixtureProc &) = delete;
	FixtureProc &operator=(const FixtureProc &) = delete;

	[[nodiscard]] const std::string &root() const noexcept { return root_; }

	void substream(const std::string &relative, const std::string &status) const
	{
		const fs::path directory = fs::path(root_) / "asound" / relative;
		fs::create_directories(directory);
		std::ofstream(directory / "status") << status;
	}
	void holder(const std::string &pid, const std::string &target) const
	{
		const fs::path directory = fs::path(root_) / pid / "fd";
		fs::create_directories(directory);
		fs::create_symlink(target, directory / "5");
	}
	void release(const std::string &pid) const
	{
		std::error_code ignored;
		fs::remove(fs::path(root_) / pid / "fd" / "5", ignored);
	}

private:
	std::string root_;
};

class RecordingSuspend final : public Suspend {
public:
	Result<void> request_suspend() override
	{
		++calls;
		return Result<void>::success();
	}
	int calls{0};
};

class FailingProbe final : public CaptureClosureProbe {
public:
	Result<CaptureClosureObservation> observe() override
	{
		return Result<CaptureClosureObservation>::failure(
			SupervisorError(ErrorCode::CaptureClosureProbeFailed, "fixture probe failure"));
	}
};

/** Calls `on_terminate(force)` so a test decides when capture closes. */
class ScriptedTerminator final : public MediaTerminator {
public:
	explicit ScriptedTerminator(std::function<void(bool)> on_terminate)
	    : on_terminate_(std::move(on_terminate))
	{
	}
	Result<void> terminate(const std::vector<int> &holders, const bool force) override
	{
		requests.push_back(force);
		last_holders = holders;
		on_terminate_(force);
		return Result<void>::success();
	}
	std::vector<bool> requests;
	std::vector<int> last_holders;

private:
	std::function<void(bool)> on_terminate_;
};

/** Deterministic time: every sleep advances the fake clock exactly. */
struct FakeTime {
	std::chrono::steady_clock::time_point now{};
	CaptureClosureGatedSuspend::Sleeper sleeper()
	{
		return [this](const std::chrono::milliseconds duration) { now += duration; };
	}
	CaptureClosureGatedSuspend::Clock clock()
	{
		return [this] { return now; };
	}
};

/** A real child process, optionally dropped to an unprivileged UID when run as root. */
class ChildProcess {
public:
	ChildProcess()
	{
		int ready[2];
		if (::pipe(ready) != 0) {
			return;
		}
		pid_ = ::fork();
		if (pid_ == 0) {
			::close(ready[0]);
			char state = 'R';
			if (::getuid() == 0 && ::setuid(65534) != 0) {
				state = 'F';
			}
			(void)!::write(ready[1], &state, 1);
			::close(ready[1]);
			if (state == 'F') {
				::_exit(3);
			}
			while (true) {
				::pause();
			}
		}
		::close(ready[1]);
		char state = 'F';
		if (pid_ > 0 && ::read(ready[0], &state, 1) == 1 && state == 'R') {
			ready_ = true;
			uid_ = ::getuid() == 0 ? 65534U : static_cast<std::uint32_t>(::getuid());
		}
		::close(ready[0]);
	}
	~ChildProcess()
	{
		if (pid_ > 0 && !reaped_) {
			::kill(pid_, SIGKILL);
			::waitpid(pid_, nullptr, 0);
		}
	}
	ChildProcess(const ChildProcess &) = delete;
	ChildProcess &operator=(const ChildProcess &) = delete;

	[[nodiscard]] bool ready() const noexcept { return ready_; }
	[[nodiscard]] int pid() const noexcept { return pid_; }
	[[nodiscard]] std::uint32_t uid() const noexcept { return uid_; }

	/** True while the child has not exited. */
	[[nodiscard]] bool alive()
	{
		int status = 0;
		return !reaped_ && ::waitpid(pid_, &status, WNOHANG) == 0;
	}
	/** Waits for exit and returns the terminating signal, or 0. */
	[[nodiscard]] int reap_signal()
	{
		int status = 0;
		if (::waitpid(pid_, &status, 0) != pid_) {
			return 0;
		}
		reaped_ = true;
		return WIFSIGNALED(status) ? WTERMSIG(status) : 0;
	}

private:
	int pid_{-1};
	bool ready_{false};
	bool reaped_{false};
	std::uint32_t uid_{0};
};

bool missing_alsa_and_no_holders_verify_closure()
{
	FixtureProc proc;
	ProcfsCaptureClosureProbe probe(proc.root());
	RecordingSuspend inner;
	CaptureClosureGatedSuspend gate(inner, probe, nullptr, CaptureClosureBounds{});
	TEST_REQUIRE(gate.request_suspend().has_value());
	TEST_REQUIRE(inner.calls == 1);
	TEST_REQUIRE(gate.last_report().verified);
	TEST_REQUIRE(!gate.last_report().termination_requested);
	return true;
}

bool closed_capture_playback_and_aliases_are_not_open()
{
	FixtureProc proc;
	proc.substream("card0/pcm0c/sub0", "closed\n");
	proc.substream("card0/pcm0p/sub0", "state: RUNNING\n");
	fs::create_directory_symlink("card0", fs::path(proc.root()) / "asound" / "Loopback");
	proc.holder("4242", "/dev/snd/pcmC0D0p");
	proc.holder("self", "/dev/snd/pcmC0D0c");
	proc.holder("0", "/dev/snd/pcmC0D0c");
	proc.holder("99999999999", "/dev/snd/pcmC0D0c");
	proc.holder("4243", "/dev/snd/controlC0");
	ProcfsCaptureClosureProbe probe(proc.root());
	auto observation = probe.observe();
	TEST_REQUIRE(observation.has_value());
	TEST_REQUIRE(observation.value().closed());
	return true;
}

bool open_substreams_and_capture_holders_are_observed()
{
	FixtureProc proc;
	proc.substream("card0/pcm0c/sub0", "state: RUNNING\nowner_pid   : 4242\n");
	proc.substream("card1/pcm3c/sub2", "garbage");
	proc.substream("card1/pcm3c/sub3", "");
	proc.holder("4242", "/dev/snd/pcmC0D0c");
	proc.holder("77", "/dev/snd/pcmC10D2c (deleted)");
	ProcfsCaptureClosureProbe probe(proc.root());
	auto observation = probe.observe();
	TEST_REQUIRE(observation.has_value());
	TEST_REQUIRE((observation.value().open_substreams ==
		      std::vector<std::string>{"card0/pcm0c/sub0", "card1/pcm3c/sub2",
					       "card1/pcm3c/sub3"}));
	TEST_REQUIRE((observation.value().holder_pids == std::vector<int>{77, 4242}));
	return true;
}

bool missing_process_table_is_a_probe_failure()
{
	ProcfsCaptureClosureProbe probe("/nonexistent/bhp-capture-closure-proc");
	const auto observation = probe.observe();
	TEST_REQUIRE(!observation.has_value());
	TEST_REQUIRE(observation.error().code() == ErrorCode::CaptureClosureProbeFailed);
	return true;
}

#ifdef __linux__
bool real_procfs_scan_succeeds()
{
	// Hidden descriptors of other users' processes are counted, not fatal.
	ProcfsCaptureClosureProbe probe;
	TEST_REQUIRE(probe.observe().has_value());
	return true;
}

#endif

bool hidden_descriptors_are_counted_not_assumed_empty()
{
	FixtureProc proc;
	proc.substream("card0/pcm0c/sub0", "closed\n");
	const fs::path hidden = fs::path(proc.root()) / "4242" / "fd";
	fs::create_directories(hidden);
	fs::permissions(hidden, fs::perms::none);
	ProcfsCaptureClosureProbe probe(proc.root());
	const auto observation = probe.observe();
	fs::permissions(hidden, fs::perms::owner_all);
	TEST_REQUIRE(observation.has_value());
	TEST_REQUIRE(observation.value().holder_pids.empty());
	// Root bypasses the fixture's DAC mode; every other user sees EACCES.
	TEST_REQUIRE(observation.value().unreadable_processes == (::getuid() == 0 ? 0U : 1U));
	return true;
}

bool open_capture_without_termination_path_refuses_suspend()
{
	FixtureProc proc;
	proc.substream("card0/pcm0c/sub0", "state: RUNNING\n");
	proc.holder("4242", "/dev/snd/pcmC0D0c");
	ProcfsCaptureClosureProbe probe(proc.root());
	RecordingSuspend inner;
	CaptureClosureGatedSuspend gate(inner, probe, nullptr, CaptureClosureBounds{});
	const auto result = gate.request_suspend();
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::CaptureTerminationUnavailable);
	TEST_REQUIRE(result.error().retryable());
	TEST_REQUIRE(result.error().context().at("initial_open_substreams") == "1");
	TEST_REQUIRE(result.error().context().at("initial_holders") == "1");
	TEST_REQUIRE(inner.calls == 0);
	TEST_REQUIRE(!gate.last_report().verified);
	return true;
}

bool probe_failure_refuses_suspend()
{
	FailingProbe probe;
	RecordingSuspend inner;
	CaptureClosureGatedSuspend gate(inner, probe, nullptr, CaptureClosureBounds{});
	const auto result = gate.request_suspend();
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::CaptureClosureProbeFailed);
	TEST_REQUIRE(inner.calls == 0);
	return true;
}

bool graceful_termination_closes_capture_before_suspend()
{
	FixtureProc proc;
	proc.substream("card0/pcm0c/sub0", "state: RUNNING\n");
	proc.holder("4242", "/dev/snd/pcmC0D0c");
	ProcfsCaptureClosureProbe probe(proc.root());
	ScriptedTerminator terminator([&proc](bool) {
		proc.substream("card0/pcm0c/sub0", "closed\n");
		proc.release("4242");
	});
	RecordingSuspend inner;
	FakeTime time;
	CaptureClosureGatedSuspend gate(inner, probe, &terminator, CaptureClosureBounds{},
					time.sleeper(), time.clock());
	TEST_REQUIRE(gate.request_suspend().has_value());
	TEST_REQUIRE(inner.calls == 1);
	TEST_REQUIRE(terminator.requests == std::vector<bool>{false});
	TEST_REQUIRE(terminator.last_holders == std::vector<int>{4242});
	TEST_REQUIRE(gate.last_report().verified);
	TEST_REQUIRE(!gate.last_report().forced);
	return true;
}

bool forced_termination_runs_after_the_grace_period()
{
	FixtureProc proc;
	proc.substream("card0/pcm0c/sub0", "state: RUNNING\n");
	ProcfsCaptureClosureProbe probe(proc.root());
	ScriptedTerminator terminator([&proc](const bool force) {
		if (force) {
			proc.substream("card0/pcm0c/sub0", "closed\n");
		}
	});
	RecordingSuspend inner;
	FakeTime time;
	const CaptureClosureBounds bounds;
	CaptureClosureGatedSuspend gate(inner, probe, &terminator, bounds, time.sleeper(),
					time.clock());
	TEST_REQUIRE(gate.request_suspend().has_value());
	TEST_REQUIRE(inner.calls == 1);
	TEST_REQUIRE((terminator.requests == std::vector<bool>{false, true}));
	TEST_REQUIRE(gate.last_report().forced);
	TEST_REQUIRE(gate.last_report().waited >= bounds.termination_grace);
	TEST_REQUIRE(gate.last_report().waited < bounds.closure_deadline);
	return true;
}

bool closure_deadline_refuses_suspend()
{
	FixtureProc proc;
	proc.substream("card0/pcm0c/sub0", "state: RUNNING\n");
	ProcfsCaptureClosureProbe probe(proc.root());
	ScriptedTerminator terminator([](bool) {});
	RecordingSuspend inner;
	FakeTime time;
	const CaptureClosureBounds bounds;
	CaptureClosureGatedSuspend gate(inner, probe, &terminator, bounds, time.sleeper(),
					time.clock());
	const auto result = gate.request_suspend();
	TEST_REQUIRE(!result.has_value());
	TEST_REQUIRE(result.error().code() == ErrorCode::CaptureClosureUnverified);
	TEST_REQUIRE(result.error().context().at("final_open_substreams") == "1");
	TEST_REQUIRE(result.error().context().at("forced") == "true");
	TEST_REQUIRE(inner.calls == 0);
	TEST_REQUIRE(gate.last_report().waited >= bounds.closure_deadline);
	TEST_REQUIRE(gate.last_report().waited < bounds.closure_deadline + bounds.poll_interval * 2);
	return true;
}

#ifdef __linux__
bool terminator_refuses_root_and_skips_exited_holders()
{
	UidScopedMediaTerminator root_terminator(0);
	const auto refused = root_terminator.terminate({1}, false);
	TEST_REQUIRE(!refused.has_value());
	TEST_REQUIRE(refused.error().code() == ErrorCode::MediaTerminationFailed);
	UidScopedMediaTerminator terminator(1103);
	// No such processes: an exited holder is not an error.
	TEST_REQUIRE(terminator.terminate({0, -4, 999999001}, true).has_value());
	return true;
}

bool terminator_signals_only_the_exact_broker_uid()
{
	ChildProcess child;
	if (!child.ready()) {
		std::cout << "capture_closure_test: skipped uid terminator (cannot drop to 65534)\n";
		return true;
	}
	UidScopedMediaTerminator other_role(child.uid() + 1U);
	TEST_REQUIRE(other_role.terminate({child.pid()}, true).has_value());
	TEST_REQUIRE(child.alive());
	UidScopedMediaTerminator broker(child.uid());
	TEST_REQUIRE(broker.terminate({child.pid()}, false).has_value());
	TEST_REQUIRE(child.reap_signal() == SIGTERM);

	ChildProcess stubborn;
	TEST_REQUIRE(stubborn.ready());
	TEST_REQUIRE(broker.terminate({stubborn.pid()}, true).has_value());
	TEST_REQUIRE(stubborn.reap_signal() == SIGKILL);
	return true;
}

#else
bool unavailable_pidfds_refuse_termination()
{
	UidScopedMediaTerminator root_terminator(0);
	TEST_REQUIRE(!root_terminator.terminate({1}, false).has_value());
	UidScopedMediaTerminator broker(1103);
	const auto refused = broker.terminate({static_cast<int>(::getpid())}, false);
	TEST_REQUIRE(!refused.has_value());
	TEST_REQUIRE(refused.error().code() == ErrorCode::MediaTerminationFailed);
	TEST_REQUIRE(refused.error().context().at("errno") == std::to_string(ENOSYS));
	return true;
}
#endif

bool bounds_are_validated()
{
	TEST_REQUIRE(validate_capture_closure_bounds(CaptureClosureBounds{}).has_value());
	CaptureClosureBounds inverted;
	inverted.closure_deadline = inverted.termination_grace;
	TEST_REQUIRE(!validate_capture_closure_bounds(inverted).has_value());
	CaptureClosureBounds unbounded;
	unbounded.closure_deadline = std::chrono::minutes(1);
	TEST_REQUIRE(!validate_capture_closure_bounds(unbounded).has_value());
	CaptureClosureBounds slow_poll;
	slow_poll.poll_interval = slow_poll.termination_grace + std::chrono::milliseconds(1);
	TEST_REQUIRE(!validate_capture_closure_bounds(slow_poll).has_value());
	CaptureClosureBounds zero_poll;
	zero_poll.poll_interval = std::chrono::milliseconds(0);
	const auto invalid = validate_capture_closure_bounds(zero_poll);
	TEST_REQUIRE(!invalid.has_value());
	TEST_REQUIRE(invalid.error().code() == ErrorCode::SuspendConfigurationInvalid);
	return true;
}

} // namespace
} // namespace house_panel::supervisor

int main()
{
	using namespace house_panel::supervisor;
	const std::vector<bool (*)()> tests{
		missing_alsa_and_no_holders_verify_closure,
		closed_capture_playback_and_aliases_are_not_open,
		open_substreams_and_capture_holders_are_observed,
		missing_process_table_is_a_probe_failure,
#ifdef __linux__
		real_procfs_scan_succeeds,
#endif
		hidden_descriptors_are_counted_not_assumed_empty,
		open_capture_without_termination_path_refuses_suspend,
		probe_failure_refuses_suspend,
		graceful_termination_closes_capture_before_suspend,
		forced_termination_runs_after_the_grace_period,
		closure_deadline_refuses_suspend,
#ifdef __linux__
		terminator_refuses_root_and_skips_exited_holders,
		terminator_signals_only_the_exact_broker_uid,
#else
		unavailable_pidfds_refuse_termination,
#endif
		bounds_are_validated,
	};
#ifndef __linux__
	std::cout << "{\"test\":\"capture_closure\",\"linux_only_cases_skipped\":3,"
		     "\"reason\":\"requires_linux_procfs_and_pidfd\"}\n";
#endif
	int passed = 0;
	for (const auto test : tests) {
		if (!test()) {
			return 1;
		}
		++passed;
	}
	std::cout << "capture_closure_test: " << passed << " passed\n";
	return 0;
}
