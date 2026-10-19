#include "bhp/durable_store/durable_store.h"
#include "test_support.h"
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <vector>
using namespace bhp::durable_store;
using test::expect;
namespace
{
const DurableFileNames kNames{"take.lock", "outbox.json", "outbox.tmp", "VOICE_LAB"};

std::string temporary_directory()
{
	const char *base = std::getenv("TMPDIR");
	std::string pattern = std::string(base ? base : "/tmp") + "/durable-store-test-XXXXXX";
	if (mkdtemp(pattern.data()) == nullptr)
		std::abort();
	return pattern;
}
} // namespace

int main()
{
	const auto root = temporary_directory();
	const auto directory = root + "/outbox";
	{
		std::vector<CommitBoundary> seen;
		DurableFile file(directory, kNames, 64, [&seen](CommitBoundary boundary) { seen.push_back(boundary); });
		auto empty = file.read();
		expect(empty && !empty.value().has_value(), "a new store reads as empty");
		expect(file.commit("{\"pending\":1}").has_value(), "commit");
		auto read = file.read();
		expect(read && read.value() == std::optional<std::string>("{\"pending\":1}"), "read what was committed");
		expect(seen == std::vector<CommitBoundary>{CommitBoundary::temporary_opened, CommitBoundary::payload_written,
							   CommitBoundary::file_synchronized, CommitBoundary::renamed,
							   CommitBoundary::directory_synchronized},
		       "the commit passes every boundary in order");
		auto full = file.commit(std::string(65, 'x'));
		expect(!full && full.error().code == "VOICE_LAB.STORAGE_FULL", "the byte ceiling uses the domain prefix");
		expect(file.read().value() == std::optional<std::string>("{\"pending\":1}"),
		       "a refused commit leaves the last state");
		struct stat status {};
		expect(stat(directory.c_str(), &status) == 0 && (status.st_mode & 0777) == 0700, "the directory is 0700");
		DurableFile second(directory, kNames, 64);
		auto locked = second.read();
		expect(!locked && locked.error().code == "VOICE_LAB.STORAGE_FAILED" &&
			       locked.error().context.at("operation") == "exclusive_lock",
		       "one writer per directory");
	}
	{
		std::ofstream(directory + "/outbox.tmp") << "half written";
		DurableFile file(directory, kNames, 64);
		expect(file.read().value() == std::optional<std::string>("{\"pending\":1}"),
		       "a leftover temporary file is never read as committed");
		expect(file.commit("{}").has_value() && !std::filesystem::exists(directory + "/outbox.tmp"),
		       "the next commit replaces the leftover");
	}
	{
		const auto open_directory = root + "/open";
		std::filesystem::create_directory(open_directory);
		std::filesystem::permissions(open_directory, std::filesystem::perms::owner_all |
								     std::filesystem::perms::group_read |
								     std::filesystem::perms::group_exec);
		DurableFile file(open_directory, kNames, 64);
		expect(!file.read() && !file.commit("x"), "a group-readable directory is refused");
		DurableFile bad(directory, {"", "a", "b", "X"}, 64);
		auto invalid = bad.read();
		expect(!invalid && invalid.error().code == "X.STORAGE_CONFIG_INVALID", "names are required");
	}
	std::filesystem::remove_all(root);
	return test::finish("durable_store");
}
