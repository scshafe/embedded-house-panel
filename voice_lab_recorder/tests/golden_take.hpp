#pragma once
// The golden take of voice-lab's pinned examples, as the media broker would
// have committed it: shared by the packager's tests, the corpus client's
// tests and the live contract client.
#include "voice_lab_recorder/take_package.h"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <sstream>
#include <string>

namespace voice_lab_recorder::golden
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

inline fs::path examples_root(const fs::path &fixtures = BHP_VOICE_LAB_FIXTURES)
{
	return fixtures / "examples";
}

inline std::string read_bytes(const fs::path &path)
{
	std::ifstream input(path, std::ios::binary);
	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}

inline void write_bytes(const fs::path &path, const std::string &bytes)
{
	std::ofstream output(path, std::ios::binary);
	output << bytes;
}

inline Json example(const std::string &name)
{
	return Json::parse(read_bytes(examples_root() / name));
}

/** PROTOCOL.md section 9: the golden take's WAV from its integer recipe (S16_LE). */
inline std::string wav_from_recipe(const Json &recipe)
{
	const unsigned channels = recipe["channels"], rate = recipe["rate"], width = 2;
	const std::uint64_t frames = recipe["frames"];
	const std::uint32_t data = static_cast<std::uint32_t>(frames * channels * width);
	std::string out;
	auto put = [&out](std::uint32_t value, unsigned bytes) {
		for (unsigned at = 0; at < bytes; ++at, value >>= 8)
			out += static_cast<char>(value & 0xffU);
	};
	out += "RIFF";
	put(36 + data, 4);
	out += "WAVEfmt ";
	put(16, 4);
	put(1, 2);
	put(channels, 2);
	put(rate, 4);
	put(rate * channels * width, 4);
	put(channels * width, 2);
	put(width * 8, 2);
	out += "data";
	put(data, 4);
	const long offset = recipe["offset"], modulus = recipe["modulus"], center = recipe["center"],
		   scale = recipe["scale"];
	for (std::uint64_t f = 0; f < frames; ++f)
		for (unsigned c = 0; c < channels; ++c) {
			const long value = ((static_cast<long>(f) * (c + 3) + offset) % modulus - center) * (c + 1) * scale;
			put(static_cast<std::uint32_t>(static_cast<std::int16_t>(value)) & 0xffffU, 2);
		}
	return out;
}

inline std::string golden_wav()
{
	return wav_from_recipe(example("take/original.wav.recipe.json"));
}

inline std::string sha256_hex(const std::string &bytes)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int size = 0;
	EVP_Digest(bytes.data(), bytes.size(), digest, &size, EVP_sha256(), nullptr);
	static const char *hex = "0123456789abcdef";
	std::string text;
	for (unsigned at = 0; at < size; ++at) {
		text += hex[digest[at] >> 4];
		text += hex[digest[at] & 15];
	}
	return text;
}

/** The broker's device record (`capture.json` `device`) that gives the golden capture profile. */
inline Json device_record()
{
	const Json profile = example("take/manifest.json")["capture_profile"];
	const Json device = profile["device"];
	return {{"model", device["model"]},
		{"usb_vendor_id", device["usb_vendor_id"]},
		{"usb_product_id", device["usb_product_id"]},
		{"serial", device["serial"]},
		{"usb_node", "/dev/bus/usb/001/004"},
		{"identity",
		 {{"firmware",
		   {{"VERSION", device["firmware"]["version"]},
		    {"BLD_MSG", device["firmware"]["build_message"]},
		    {"BLD_REPO_HASH", device["firmware"]["build_repo_hash"]}}},
		  {"settings", profile["settings"]},
		  {"routing", Json::object()},
		  {"unread", Json::object()}}}};
}

/** Another take of the golden recipe (`offset` shifts the sawtooth): new audio, a new take id. */
inline std::string variant_wav(long offset)
{
	Json recipe = example("take/original.wav.recipe.json");
	recipe["offset"] = offset;
	return wav_from_recipe(recipe);
}

/**
 * Writes a take into `<broker_root>/<take id>/` in the take store's format,
 * with the golden take's continuity, and `device` as its device record.
 */
inline fs::path broker_take(const fs::path &broker_root, const std::string &wav, const Json &device = nullptr)
{
	const Json continuity = example("take/continuity.json");
	const std::string sha256 = sha256_hex(wav);
	const std::string take_id = "take-" + sha256;
	const fs::path directory = broker_root / take_id;
	fs::create_directories(directory);
	write_bytes(directory / "original.wav", wav);
	Json capture = continuity;
	capture.erase("success");
	capture.erase("error");
	capture["schema_version"] = "bhp.media.voice-take/v1";
	capture["take_id"] = take_id;
	capture["purpose"] = "voice_take";
	capture["wav"]["sha256"] = sha256;
	capture["wav"]["size_bytes"] = wav.size();
	capture["device"] = device;
	write_bytes(directory / "capture.json", capture.dump() + "\n");
	return directory;
}

/** What the recorder knew when the golden take was recorded. */
inline TakePackageInputs inputs()
{
	const Json manifest = example("take/manifest.json");
	TakePackageInputs inputs;
	inputs.prompt = manifest["prompt"];
	inputs.catalog = manifest["catalog"];
	inputs.capture_profile = manifest["capture_profile"];
	inputs.client = manifest["client"];
	inputs.capture_host = manifest["capture_host"];
	inputs.requested_at = manifest["requested_at"];
	inputs.qc_generated_at = example("take/qc.json")["generated_at"];
	inputs.finalized_at = example("take/READY")["finalized_at"];
	return inputs;
}
} // namespace voice_lab_recorder::golden
