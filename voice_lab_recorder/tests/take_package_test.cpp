// Canonical JSON and the corpus-take/v2 packager against voice-lab's pinned
// examples (voice_lab_recorder/tests/fixtures/corpus_client_v1/).
#include "voice_lab_recorder/canonical_json.h"
#include "voice_lab_recorder/take_package.h"
#include "golden_take.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace voice_lab_recorder;
using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
int failures = 0;

void expect(const bool condition, const std::string &what)
{
	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << what << '\n';
	}
}

using golden::example;
using golden::read_bytes;
using golden::wav_from_recipe;
using golden::write_bytes;
const fs::path kExamples = golden::examples_root();

struct Scratch {
	fs::path root;
	Scratch()
	{
		std::string pattern = (fs::temp_directory_path() / "bhp-take-package-XXXXXX").string();
		root = ::mkdtemp(pattern.data());
		fs::create_directory(root / "broker");
		fs::create_directory(root / "outbox");
	}
	~Scratch() { fs::remove_all(root); }
};

fs::path broker_take(const Scratch &scratch, const std::string &wav)
{
	return golden::broker_take(scratch.root / "broker", wav);
}

TakePackageInputs golden_inputs()
{
	return golden::inputs();
}

void canonical_vectors()
{
	for (const auto &vector : example("canonical-json-vectors.json")["vectors"]) {
		const auto text = canonical_json(vector["value"]);
		expect(text.has_value() && text.value() == vector["canonical"].get<std::string>(),
		       "canonical vector " + vector["canonical"].get<std::string>());
	}
	const auto record = canonical_record(Json{{"b", 1}, {"a", 0.5}});
	expect(record.has_value() && record.value() == "{\"a\":0.5,\"b\":1}\n", "a record ends with one newline");
	expect(!canonical_json(Json(std::numeric_limits<double>::quiet_NaN())).has_value(), "NaN is refused");
	expect(!canonical_json(Json::array({std::numeric_limits<double>::infinity()})).has_value(),
	       "infinity is refused");
}

void python_repr()
{
	const std::vector<std::pair<double, std::string>> cases = {
		{1e15, "1000000000000000.0"},  {1e16, "1e+16"},
		{9999999999999998.0, "9999999999999998.0"}, {957869953261499.2, "957869953261499.2"},
		{5.514840470082085e-18, "5.514840470082085e-18"}, {0.0001, "0.0001"},
		{1e-05, "1e-05"},             {-0.0, "-0.0"},
		{100.0, "100.0"},             {1.2345678901234568e+17, "1.2345678901234568e+17"},
		{5e-324, "5e-324"},           {1.7976931348623157e308, "1.7976931348623157e+308"},
		{31.6, "31.6"},               {-0.006450265645980835, "-0.006450265645980835"},
	};
	for (const auto &[value, text] : cases)
		expect(python_float_repr(value) == text, "repr " + text + " got " + python_float_repr(value));
}

void golden_take()
{
	Scratch scratch;
	const std::string wav = wav_from_recipe(example("take/original.wav.recipe.json"));
	const fs::path take = broker_take(scratch, wav);
	const auto packaged = write_take_package(take.string(), (scratch.root / "outbox").string(), golden_inputs());
	if (!packaged.has_value()) {
		expect(false, "golden take packaged: " + packaged.error().code);
		return;
	}
	const fs::path package = packaged.value().directory;
	expect(packaged.value().take_id == take.filename().string(), "package named by the take id");
	expect(packaged.value().qc_status == "pass" && packaged.value().frames == 4096, "golden QC passes");
	for (const char *name : {"manifest.json", "qc.json", "READY", "continuity.json"})
		expect(read_bytes(package / name) == read_bytes(kExamples / "take" / name),
		       std::string("golden ") + name + " byte for byte");
	expect(read_bytes(package / "original.wav") == wav, "original.wav unchanged");
	std::vector<std::string> names;
	for (const auto &entry : fs::directory_iterator(scratch.root / "outbox"))
		names.push_back(entry.path().filename().string());
	expect(names.size() == 1, "only the committed package in the outbox");
	std::size_t files = 0;
	for (const auto &entry : fs::directory_iterator(package)) {
		++files;
		struct stat status {};
		::stat(entry.path().c_str(), &status);
		expect((status.st_mode & 0777) == 0600, "package file is owner-only");
	}
	expect(files == 5, "exactly five files");

	const auto again = write_take_package(take.string(), (scratch.root / "outbox").string(), golden_inputs());
	expect(!again.has_value() && again.error().code == "VOICE_LAB.TAKE_PACKAGE_EXISTS", "a package is written once");
}

void refusals()
{
	Scratch scratch;
	const std::string wav = wav_from_recipe(example("take/original.wav.recipe.json"));
	const fs::path take = broker_take(scratch, wav);
	const std::string outbox = (scratch.root / "outbox").string();

	auto inputs = golden_inputs();
	inputs.capture_profile["channels"].erase(0);
	auto refused = write_take_package(take.string(), outbox, inputs);
	expect(!refused.has_value() && refused.error().code == "VOICE_LAB.TAKE_PROFILE_MISMATCH",
	       "a channel map that does not fit is refused");
	inputs = golden_inputs();
	inputs.capture_profile["primary_channel"] = 7;
	refused = write_take_package(take.string(), outbox, inputs);
	expect(!refused.has_value() && refused.error().code == "VOICE_LAB.TAKE_PROFILE_MISMATCH",
	       "a primary channel outside the WAV is refused");

	std::string changed = wav;
	changed[100] = static_cast<char>(changed[100] ^ 1);
	write_bytes(take / "original.wav", changed);
	refused = write_take_package(take.string(), outbox, golden_inputs());
	expect(!refused.has_value() && refused.error().code == "VOICE_LAB.TAKE_RECORD_INVALID",
	       "audio that is not the take id is refused");
	expect(fs::is_empty(outbox), "a refused take leaves nothing in the outbox");
	write_bytes(take / "original.wav", wav);

	Json capture = Json::parse(read_bytes(take / "capture.json"));
	capture["xruns"] = 1;
	write_bytes(take / "capture.json", capture.dump());
	refused = write_take_package(take.string(), outbox, golden_inputs());
	expect(!refused.has_value() && refused.error().code == "VOICE_LAB.TAKE_RECORD_INVALID", "an xrun is refused");
	capture["xruns"] = 0;
	capture["periods"][2][1] = capture["periods"][2][1].get<std::uint64_t>() + 5'000'000;
	write_bytes(take / "capture.json", capture.dump());
	refused = write_take_package(take.string(), outbox, golden_inputs());
	expect(!refused.has_value() && refused.error().code == "VOICE_LAB.TAKE_RECORD_INVALID",
	       "continuity beyond the recorded deviation is refused");
	expect(fs::is_empty(outbox), "still nothing in the outbox");
}

void quality_findings()
{
	Scratch scratch;
	const fs::path path = scratch.root / "silent.wav";
	Json recipe = example("take/original.wav.recipe.json");
	recipe["frames"] = 8000; // 0.5 s
	recipe["scale"] = 0;
	write_bytes(path, wav_from_recipe(recipe));
	const auto silent = inspect_wav(path.string(), 3);
	expect(silent.has_value() && silent.value().peak_dbfs == -300.0 && silent.value().longest_zero_run_ms == 500.0,
	       "a silent channel is the floor with one long zero run");
	if (silent.has_value()) {
		const auto metrics = qc_metrics(silent.value(), false);
		expect(metrics["findings"] == Json::array({"peak-too-quiet", "digital-silence-or-dropout"}) &&
			       !metrics["passed"].get<bool>(),
		       "silence is quiet and a dropout");
		expect(qc_metrics(silent.value(), true)["findings"] == Json::array({"digital-silence-or-dropout"}),
		       "a silent prompt allows quiet, not dropouts");
	}
	expect(silent_prompt({{"category", "room-tone"}}) && !silent_prompt({{"category", "reference"}}),
	       "room tone is silent, reference is spoken");
	expect(!inspect_wav(path.string(), 7).has_value(), "no channel 7 in a six-channel WAV");
	write_bytes(path, "RIFF");
	expect(!inspect_wav(path.string(), 1).has_value(), "a truncated header is refused");
}
void capture_profile_from_the_device_record()
{
	const Json golden = example("take/manifest.json")["capture_profile"];
	const Json device = golden["device"];
	Json record = {{"model", device["model"]},
		       {"usb_vendor_id", device["usb_vendor_id"]},
		       {"usb_product_id", device["usb_product_id"]},
		       {"serial", device["serial"]},
		       {"usb_node", "/dev/bus/usb/001/004"},
		       {"identity",
			{{"firmware",
			  {{"VERSION", device["firmware"]["version"]},
			   {"BLD_MSG", device["firmware"]["build_message"]},
			   {"BLD_REPO_HASH", device["firmware"]["build_repo_hash"]}}},
			 {"settings", golden["settings"]},
			 {"routing", {{"AUDIO_MGR_OP_L", {8, 0}}}},
			 {"unread", Json::object()}}}};
	const auto profile = xvf3800_capture_profile(record, golden["channels"], golden["room"],
						     golden["primary_channel"].get<unsigned>());
	expect(profile.has_value() && profile.value() == golden, "the golden capture profile from a device record");
	Json no_serial = record;
	no_serial["serial"] = nullptr;
	const auto unread = xvf3800_capture_profile(no_serial, golden["channels"], golden["room"], 3);
	expect(!unread.has_value() && unread.error().code == "VOICE_LAB.DEVICE_UNREAD", "an unread serial is refused");
	Json missing = record;
	missing["identity"]["settings"]["PP_MIN_NS"] = nullptr;
	expect(!xvf3800_capture_profile(missing, golden["channels"], golden["room"], 3).has_value(),
	       "an unread setting is refused");
	expect(!xvf3800_capture_profile(record, golden["channels"], golden["room"], 9).has_value(),
	       "a primary channel outside the map is refused");
}
} // namespace

int main()
{
	canonical_vectors();
	python_repr();
	golden_take();
	refusals();
	quality_findings();
	capture_profile_from_the_device_record();
	if (failures)
		std::cerr << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
