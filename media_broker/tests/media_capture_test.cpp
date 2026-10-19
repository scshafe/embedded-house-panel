#include "media_broker/capture_source.h"
#include "media_broker/continuity_log.h"
#include "media_broker/take_store.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <openssl/evp.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
using namespace media_broker;
namespace
{
int failures = 0;
void expect(const bool value, const std::string &label)
{
	if (!value) {
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
}

std::vector<std::string> temporary_roots;

std::string temporary_root()
{
	const char *base = std::getenv("TMPDIR");
	std::string pattern = std::string(base ? base : "/tmp") + "/media-capture-test-XXXXXX";
	if (mkdtemp(pattern.data()) == nullptr)
		std::abort();
	temporary_roots.push_back(pattern);
	return pattern;
}

std::string read_file(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(file)), {});
}

std::string sha256_hex(const std::string &bytes)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr);
	std::string text;
	for (unsigned at = 0; at < size; ++at) {
		text += "0123456789abcdef"[digest[at] >> 4];
		text += "0123456789abcdef"[digest[at] & 15];
	}
	return text;
}

std::uint32_t le32(const std::string &bytes, std::size_t at)
{
	return static_cast<std::uint8_t>(bytes[at]) | static_cast<std::uint8_t>(bytes[at + 1]) << 8 |
	       static_cast<std::uint8_t>(bytes[at + 2]) << 16 |
	       static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at + 3])) << 24;
}

const TakeLimits kLimits{4, 64ULL << 20, 0};
const CaptureRequest kRequest{"hw:CARD=Fixture,DEV=0", SampleFormat::S16_LE, 16000, 6, 1024, 4096};

TakeSettings settings(std::uint64_t maximum_frames = 16000 * 600)
{
	return {"2026-10-19T12:00:00Z", 1'000'000, maximum_frames, {{"model", "fixture"}}};
}

/** Records `periods` periods from `source` and finishes with `reason`. */
MediaResult<TakeSummary> record(TakeStore &store, FakeCaptureSource &source, unsigned periods,
				TakeSettings take_settings = settings(), StopReason reason = StopReason::Requested)
{
	auto format = source.open(kRequest);
	if (!format)
		return MediaResult<TakeSummary>::failure(format.error());
	auto writer = TakeWriter::begin(store, format.value(), std::move(take_settings));
	if (!writer)
		return MediaResult<TakeSummary>::failure(writer.error());
	std::vector<std::uint8_t> samples;
	for (unsigned index = 0; index < periods; ++index) {
		auto period = source.read_period(samples);
		if (!period) {
			if (period.error().code == "MEDIA_CAPTURE.XRUN") {
				writer.value()->count_xrun();
				break;
			}
			source.close();
			return MediaResult<TakeSummary>::failure(period.error());
		}
		auto appended = writer.value()->append(samples, period.value());
		if (!appended) {
			source.close();
			return MediaResult<TakeSummary>::failure(appended.error());
		}
	}
	source.close();
	return writer.value()->finish(reason);
}

bool only_takes(const std::string &root, std::size_t count)
{
	auto store = TakeStore::open(root, kLimits);
	return store && store.value().take_ids().size() == count;
}

void test_wav_header()
{
	CaptureFormat format{"x", SampleFormat::S16_LE, 16000, 6, 1024, 4096, TimestampType::MonotonicRaw, false};
	const auto header = wav_header(format, 1000);
	const std::string bytes(header.begin(), header.end());
	expect(header.size() == 44, "header is 44 bytes");
	expect(bytes.substr(0, 4) == "RIFF" && bytes.substr(8, 8) == "WAVEfmt " && bytes.substr(36, 4) == "data",
	       "header tags");
	expect(le32(bytes, 4) == 36 + 12000 && le32(bytes, 40) == 12000, "RIFF and data sizes");
	expect(le32(bytes, 24) == 16000 && le32(bytes, 28) == 16000 * 12, "rate and byte rate");
	expect(static_cast<std::uint8_t>(bytes[22]) == 6 && static_cast<std::uint8_t>(bytes[32]) == 12 &&
		       static_cast<std::uint8_t>(bytes[34]) == 16,
	       "channels, block align, bits");
}

void test_continuity_rule()
{
	expect(!ContinuityLog::create(44100, 10), "a rate that does not divide 1e9 is refused");
	auto log = ContinuityLog::create(16000, 10);
	expect(log && log.value().ns_per_frame() == 62500, "16 kHz is 62500 ns per frame");
	auto &continuity = log.value();
	expect(continuity.add({1024, 1'000'000'000}).has_value(), "first pair");
	expect(!continuity.max_abs_deviation_ns(), "one pair has no deviation");
	expect(continuity.add({2048, 1'064'001'000}).has_value(), "late pair");
	expect(continuity.add({3072, 1'127'999'500}).has_value(), "early pair");
	expect(continuity.max_abs_deviation_ns() == std::optional<std::uint64_t>(1500),
	       "max |(ts - ts') - (pos - pos') * 62500| is 1500 ns");
	auto order = continuity.add({3072, 1'200'000'000});
	expect(!order && order.error().code == "MEDIA_CAPTURE.CONTINUITY_ORDER", "a repeated position is refused");
	expect(!continuity.add({4096, 1'127'999'500}), "a repeated timestamp is refused");
}

void test_fake_source_and_peaks()
{
	FakeCaptureSource source;
	auto format = source.open(kRequest);
	expect(format && format.value().simulated && format.value().period_bytes() == 1024 * 12, "fake grants");
	std::vector<std::uint8_t> samples;
	auto first = source.read_period(samples);
	expect(first && first.value().position_frames == 1024 &&
		       first.value().timestamp_ns == 1'000'000'000 + 1024 * 62500,
	       "first period position and timestamp");
	const auto value = static_cast<std::int16_t>(samples[2] | samples[3] << 8);
	expect(value == FakeCaptureSource::sample_at(1, 0, 1), "interleaved channel 2 of frame 0");
	const auto peaks = period_peaks_dbfs(format.value(), samples);
	expect(peaks.size() == 6 && peaks[0] > -30.0 && peaks[0] < -20.0, "peak of a 2000-count tone");
	FakeCaptureSource refused(FakeCaptureFaults{{}, {}, {}, true});
	expect(!refused.open(kRequest), "refuse_open");
	auto stereo48 = kRequest;
	stereo48.format = SampleFormat::S32_LE;
	expect(!FakeCaptureSource().open(stereo48), "the fake grants only S16_LE");
}

void test_clean_take_commits_whole()
{
	const auto root = temporary_root() + "/takes";
	auto store = TakeStore::open(root, kLimits);
	expect(store.has_value(), "store opens and creates its root");
	FakeCaptureSource source;
	auto summary = record(store.value(), source, 16, settings(), StopReason::MaximumDuration);
	expect(summary.has_value(), "clean take commits");
	if (!summary)
		return;
	const auto directory = root + "/" + summary.value().take_id;
	const auto wav = read_file(directory + "/original.wav");
	expect(summary.value().frames == 16 * 1024 && wav.size() == 44 + 16 * 1024 * 12 &&
		       summary.value().wav_bytes == wav.size(),
	       "size is 44 + frames * channels * 2");
	expect(summary.value().take_id == "take-" + sha256_hex(wav), "take ID is the audio's SHA-256");
	const auto text = read_file(directory + "/capture.json");
	const auto record_json = nlohmann::json::parse(text);
	expect(text == record_json.dump() + "\n", "record is compact, sorted and ends with a newline");
	expect(record_json["schema_version"] == kTakeRecordSchema && record_json["frames"] == 16 * 1024 &&
		       record_json["periods"].size() == 16 && record_json["xruns"] == 0 &&
		       record_json["max_abs_deviation_ns"] == 0 && record_json["simulated"] == true &&
		       record_json["stop_reason"] == "maximum-duration" &&
		       record_json["wav"]["size_bytes"] == wav.size() && record_json["device"]["model"] == "fixture",
	       "record fields");
	expect(record_json["periods"][0] == nlohmann::json::array({1024, 1'000'000'000 + 1024 * 62500}),
	       "period pairs");
	struct stat status {};
	expect(stat(root.c_str(), &status) == 0 && (status.st_mode & 0777) == 0700, "root is 0700");
	expect(only_takes(root, 1), "one take, no partials");
	FakeCaptureSource same;
	auto duplicate = record(store.value(), same, 16);
	expect(!duplicate && duplicate.error().code == "MEDIA_TAKE.DUPLICATE", "identical audio is a duplicate");
	FakeCaptureSource other({}, false, 1'000'000'000, 2);
	expect(record(store.value(), other, 16).has_value(), "another seed is another take");
	expect(only_takes(root, 2), "two takes after the duplicate was erased");
}

void test_failed_takes_leave_nothing()
{
	const auto root = temporary_root() + "/takes";
	auto store = TakeStore::open(root, kLimits).value();
	FakeCaptureSource xrun(FakeCaptureFaults{3, {}, {}, false});
	auto overran = record(store, xrun, 10);
	expect(!overran && overran.error().code == "MEDIA_CAPTURE.XRUN", "an xrun fails the take");
	FakeCaptureSource lost(FakeCaptureFaults{{}, 2, {}, false});
	expect(!record(store, lost, 10), "a lost device fails the take");
	FakeCaptureSource jump(FakeCaptureFaults{{}, {}, std::make_pair(5ULL, 2'000'000LL), false});
	auto deviated = record(store, jump, 10);
	expect(!deviated && deviated.error().code == "MEDIA_CAPTURE.CONTINUITY_DEVIATION",
	       "a 2 ms jump over a 1 ms tolerance fails the take");
	FakeCaptureSource longer;
	auto too_long = record(store, longer, 10, settings(4 * 1024));
	expect(!too_long && too_long.error().code == "MEDIA_TAKE.TOO_LONG", "the writer stops at its maximum");
	FakeCaptureSource empty;
	auto nothing = record(store, empty, 0);
	expect(!nothing && nothing.error().code == "MEDIA_TAKE.EMPTY", "an empty take is refused");
	{
		FakeCaptureSource source;
		auto writer = TakeWriter::begin(store, source.open(kRequest).value(), settings());
		std::vector<std::uint8_t> samples;
		const auto period = source.read_period(samples);
		expect(writer.value()->append(samples, period.value()).has_value(), "a partial take");
	}
	expect(only_takes(root, 0) && store.take_ids().empty(), "failed and destroyed takes leave nothing");
	std::string listing;
	for (const auto &name : {".incoming-a", ".incoming-b"})
		listing += access((root + "/" + name).c_str(), F_OK) == 0 ? name : "";
	expect(listing.empty(), "no partial directories remain");
}

void test_store_bounds_and_safety()
{
	const auto base = temporary_root();
	expect(!TakeStore::open("relative/takes", kLimits), "a relative root is refused");
	const auto open_root = base + "/open";
	mkdir(open_root.c_str(), 0755);
	auto unsafe = TakeStore::open(open_root, kLimits);
	expect(!unsafe && unsafe.error().code == "MEDIA_TAKE.STORE_UNSAFE", "a group-readable root is refused");
	const auto root = base + "/takes";
	mkdir(root.c_str(), 0700);
	const auto leftover = root + "/.incoming-crash";
	mkdir(leftover.c_str(), 0700);
	std::ofstream(leftover + "/original.wav") << "partial";
	auto store = TakeStore::open(root, {1, 64ULL << 20, 0});
	expect(store && access(leftover.c_str(), F_OK) != 0, "a crash leftover is removed at open");
	expect(store.value().admit(1 << 20).has_value(), "room for one take");
	FakeCaptureSource source;
	expect(record(store.value(), source, 4).has_value(), "one take recorded");
	auto full = store.value().admit(1 << 20);
	expect(!full && full.error().code == "MEDIA_TAKE.STORE_FULL", "the count ceiling refuses another");
	auto bytes = TakeStore::open(base + "/bytes", {4, 1 << 20, 0});
	auto big = bytes.value().admit(2 << 20);
	expect(!big && big.error().code == "MEDIA_TAKE.STORE_FULL", "the byte ceiling refuses a long take");
	auto space = TakeStore::open(base + "/space", {4, ~0ULL >> 1, ~0ULL >> 2});
	auto no_space = space.value().admit(1);
	expect(!no_space && no_space.error().code == "MEDIA_TAKE.NO_SPACE", "the free-space margin refuses");
}
} // namespace

int main()
{
	test_wav_header();
	test_continuity_rule();
	test_fake_source_and_peaks();
	test_clean_take_commits_whole();
	test_failed_takes_leave_nothing();
	test_store_bounds_and_safety();
	for (const auto &root : temporary_roots)
		std::filesystem::remove_all(root);
	std::cout << "media_capture: " << failures << " failures\n";
	return failures ? 1 : 0;
}
