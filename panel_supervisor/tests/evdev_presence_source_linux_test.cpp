#include "panel_supervisor/evdev_presence_source.hpp"

#include <array>
#include <climits>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <linux/input.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace house_panel::supervisor {
namespace {

#define TEST_REQUIRE(condition)                                                                    \
	do {                                                                                         \
		if (!(condition)) {                                                                    \
			std::cerr << __func__ << ": requirement failed: " #condition << '\n';          \
			return false;                                                                    \
		}                                                                                    \
	} while (false)

bool write_exact_state(const std::string &state_path, const char state)
{
	int flags = O_WRONLY | O_TRUNC;
#ifdef O_CLOEXEC
	flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	const int descriptor = ::open(state_path.c_str(), flags);
	if (descriptor < 0) {
		return false;
	}
	const bool write_succeeded = ::write(descriptor, &state, 1) == 1;
	return ::close(descriptor) == 0 && write_succeeded;
}

std::vector<std::string> read_request_ledger(const std::string &request_log_path)
{
	std::ifstream input(request_log_path);
	return std::vector<std::string>(std::istream_iterator<std::string>(input),
					std::istream_iterator<std::string>());
}

bool asserted_and_deasserted_state_use_exact_linux_ioctl_requests(
	const std::string &device_path, const std::string &state_path,
	const std::string &request_log_path)
{
	auto source_result = EvdevPresenceSource::create(device_path);
	TEST_REQUIRE(source_result.has_value());
	auto source = std::move(source_result).value();

	TEST_REQUIRE(write_exact_state(state_path, '1'));
	const auto asserted_result = source.presence_detected();
	TEST_REQUIRE(asserted_result.has_value());
	TEST_REQUIRE(asserted_result.value());

	TEST_REQUIRE(write_exact_state(state_path, '0'));
	const auto deasserted_result = source.presence_detected();
	TEST_REQUIRE(deasserted_result.has_value());
	TEST_REQUIRE(!deasserted_result.value());

	constexpr std::size_t bits_per_word = sizeof(unsigned long) * CHAR_BIT;
	constexpr std::size_t switch_word_count = (SW_MAX + bits_per_word) / bits_per_word;
	constexpr std::size_t switch_bytes = sizeof(unsigned long) * switch_word_count;
	const std::string capability_request =
		"EVIOCGBIT_EV_SW=" +
		std::to_string(static_cast<unsigned long>(EVIOCGBIT(EV_SW, switch_bytes)));
	const std::string state_request =
		"EVIOCGSW=" +
		std::to_string(static_cast<unsigned long>(EVIOCGSW(switch_bytes)));
	const std::vector<std::string> expected_requests{
		capability_request, state_request, capability_request, state_request};
	TEST_REQUIRE(read_request_ledger(request_log_path) == expected_requests);
	return true;
}

} // namespace
} // namespace house_panel::supervisor

int main(const int argument_count, char **argument_values)
{
	using namespace house_panel::supervisor;
	if (argument_count != 4) {
		std::cerr << "Usage: evdev_presence_source_linux_test DEVICE STATE REQUEST_LOG\n";
		return 2;
	}
	if (!asserted_and_deasserted_state_use_exact_linux_ioctl_requests(
		    argument_values[1], argument_values[2], argument_values[3])) {
		return 1;
	}
	std::cout << "evdev_presence_source_linux_test: exact EVIOCGBIT and EVIOCGSW requests observed\n";
	return 0;
}
