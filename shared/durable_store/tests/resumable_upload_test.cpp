#include "bhp/durable_store/durable_store.h"
#include "test_support.h"
#include <string>
using namespace bhp::durable_store;
using test::expect;
namespace
{
/** A server that keeps bytes in order and can be told to fail or to keep part of a chunk. */
struct FakeServer {
	std::string file;
	std::string received;
	int fail_next_put{0};       // 0 none, 1 retryable, 2 refusal
	std::size_t keep_on_failure{0}; // bytes of the failing chunk it keeps anyway
	bool fail_status{false};
	std::uint64_t lie_offset{0};
	int puts{0};
	UploadPorts ports()
	{
		return {[this]() -> Result<std::uint64_t> {
				if (fail_status)
					return Result<std::uint64_t>::failure({"INTAKE.UNAVAILABLE", "down", {}, true});
				if (lie_offset)
					return Result<std::uint64_t>::success(lie_offset);
				return Result<std::uint64_t>::success(received.size());
			},
			[this](std::uint64_t offset, std::uint64_t length) -> Result<Done> {
				++puts;
				if (offset != received.size())
					return Result<Done>::failure({"INTAKE.RANGE_NOT_CONTIGUOUS", "gap", {}, false});
				if (fail_next_put) {
					const bool retryable = fail_next_put == 1;
					fail_next_put = 0;
					received += file.substr(offset, keep_on_failure);
					return Result<Done>::failure({retryable ? "INTAKE.RESET" : "INTAKE.CHECKSUM_MISMATCH",
								      "failed", {}, retryable});
				}
				received += file.substr(offset, length);
				return Result<Done>::success({});
			}};
	}
};

bool run(ResumableUpload &upload, int limit = 100)
{
	for (int i = 0; i < limit && !upload.complete() && !upload.stopped(); ++i)
		(void)upload.step();
	return upload.complete();
}
} // namespace

int main()
{
	expect(!ResumableUpload::create(10, 0, FakeServer().ports()), "a zero chunk");
	{
		FakeServer server;
		server.file = std::string(10000, 'a') + std::string(5, 'b');
		auto upload = ResumableUpload::create(server.file.size(), 4096, server.ports()).value();
		auto first = upload.step();
		expect(first && first.value().server_offset == 0 && server.puts == 0, "the first step asks the server");
		expect(run(upload) && server.received == server.file && server.puts == 3, "three chunks, in order");
	}
	{
		FakeServer server;
		server.file = std::string(10000, 'c');
		server.received = server.file.substr(0, 5000);
		auto upload = ResumableUpload::create(server.file.size(), 4096, server.ports()).value();
		expect(run(upload) && server.received == server.file && server.puts == 2,
		       "an interrupted upload resumes from the server's offset");
	}
	{
		FakeServer server;
		server.file = std::string(10000, 'd');
		auto upload = ResumableUpload::create(server.file.size(), 4096, server.ports()).value();
		(void)upload.step();
		server.fail_next_put = 1;
		server.keep_on_failure = 1000;
		auto failed = upload.step();
		expect(!failed && failed.error().retryable && !upload.stopped(), "a retryable failure is returned");
		auto asked = upload.step();
		expect(asked && asked.value().server_offset == 1000, "after a failure the driver asks the server again");
		expect(run(upload) && server.received == server.file, "and resends only what the server lacks");
	}
	{
		FakeServer server;
		server.file = std::string(100, 'e');
		auto upload = ResumableUpload::create(server.file.size(), 64, server.ports()).value();
		(void)upload.step();
		server.fail_next_put = 2;
		auto refused = upload.step();
		expect(!refused && !refused.error().retryable && upload.stopped(), "a refusal stops the upload");
		auto after = upload.step();
		expect(!after && after.error().code == "UPLOAD.STOPPED", "and it stays stopped");
	}
	{
		FakeServer server;
		server.file = std::string(100, 'f');
		server.lie_offset = 200;
		auto upload = ResumableUpload::create(server.file.size(), 64, server.ports()).value();
		auto invalid = upload.step();
		expect(!invalid && invalid.error().code == "UPLOAD.SERVER_OFFSET_INVALID" && upload.stopped(),
		       "an offset beyond the file stops the upload");
	}
	{
		FakeServer server;
		server.file = std::string(100, 'g');
		server.fail_status = true;
		auto upload = ResumableUpload::create(server.file.size(), 64, server.ports()).value();
		auto down = upload.step();
		expect(!down && down.error().retryable && !upload.stopped(), "an unavailable status is retryable");
		server.fail_status = false;
		expect(run(upload) && server.received == server.file, "and the upload continues once it answers");
	}
	{
		FakeServer server;
		auto empty = ResumableUpload::create(0, 64, server.ports()).value();
		auto state = empty.step();
		expect(state && state.value().complete && empty.complete() && server.puts == 0, "an empty file is complete");
	}
	return test::finish("resumable_upload");
}
