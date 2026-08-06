#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace house_panel::supervisor::test_fixture {

class TemporaryDirectory final {
public:
	TemporaryDirectory()
	{
		char path_template[] = "/tmp/bhp-panel-supervisor-test-XXXXXX";
		char *created_path = ::mkdtemp(path_template);
		if (created_path == nullptr) {
			throw std::runtime_error(
				"failed to create panel supervisor test directory");
		}
		path_ = created_path;
	}

	~TemporaryDirectory()
	{
		std::error_code removal_error;
		(void)std::filesystem::remove_all(path_, removal_error);
	}

	TemporaryDirectory(const TemporaryDirectory &) = delete;
	TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
	TemporaryDirectory(TemporaryDirectory &&) = delete;
	TemporaryDirectory &operator=(TemporaryDirectory &&) = delete;

	[[nodiscard]] const std::string &path() const noexcept { return path_; }

	[[nodiscard]] std::string child_path(const std::string_view name) const
	{
		if (name.empty() || name.find('/') != std::string_view::npos) {
			throw std::invalid_argument(
				"temporary child name must be one path segment");
		}
		return path_ + "/" + std::string(name);
	}

	void write_text(const std::string_view name, const std::string &content) const
	{
		std::ofstream output(child_path(name), std::ios::binary | std::ios::trunc);
		if (!output) {
			throw std::runtime_error("failed to create panel supervisor test file");
		}
		output << content;
		if (!output) {
			throw std::runtime_error("failed to write panel supervisor test file");
		}
	}

	[[nodiscard]] std::string read_text(const std::string_view name) const
	{
		std::ifstream input(child_path(name), std::ios::binary);
		if (!input) {
			throw std::runtime_error("failed to read panel supervisor test file");
		}
		return std::string(std::istreambuf_iterator<char>(input),
				   std::istreambuf_iterator<char>());
	}

private:
	std::string path_;
};

} // namespace house_panel::supervisor::test_fixture
