#include "bug_reporter/description_audio.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <vector>

namespace bug_reporter
{
namespace
{
std::uint32_t le32(const std::string &bytes, std::size_t at)
{
	return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at])) |
	       static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 1])) << 8 |
	       static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 2])) << 16 |
	       static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + 3])) << 24;
}

std::uint16_t le16(const std::string &bytes, std::size_t at)
{
	return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at]) |
					  static_cast<unsigned char>(bytes[at + 1]) << 8);
}

void put32(std::string &out, std::uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8)
		out += static_cast<char>((value >> shift) & 0xff);
}

void put16(std::string &out, std::uint16_t value)
{
	out += static_cast<char>(value & 0xff);
	out += static_cast<char>(value >> 8);
}

Error invalid(const char *why)
{
	return {"REPORT.AUDIO_INVALID", "The spoken description is not a usable take", {{"why", why}}, false};
}
} // namespace

bool valid_take_id(const std::string &take_id)
{
	static const std::regex pattern("take-[0-9a-f]{64}");
	return std::regex_match(take_id, pattern);
}

Result<std::string> mono_description(const std::string &wav_path, unsigned channel, std::uint64_t maximum_frames)
{
	using Output = Result<std::string>;
	std::ifstream input(wav_path, std::ios::binary);
	if (!input)
		return Output::failure(invalid("missing"));
	const std::string wav{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	if (wav.size() < 44 || wav.compare(0, 4, "RIFF") != 0 || wav.compare(8, 4, "WAVE") != 0 ||
	    wav.compare(12, 4, "fmt ") != 0 || wav.compare(36, 4, "data") != 0)
		return Output::failure(invalid("header"));
	const auto format = le16(wav, 20), channels = le16(wav, 22), bits = le16(wav, 34);
	const auto rate = le32(wav, 24);
	if (format != 1 || bits != 16 || channels == 0 || channel >= channels || rate == 0)
		return Output::failure(invalid("format"));
	const std::uint64_t frame_bytes = 2ULL * channels;
	const std::uint64_t data_bytes = std::min<std::uint64_t>(le32(wav, 40), wav.size() - 44);
	const std::uint64_t frames = std::min<std::uint64_t>(data_bytes / frame_bytes, maximum_frames);
	std::string out;
	out.reserve(44 + frames * 2);
	out += "RIFF";
	put32(out, static_cast<std::uint32_t>(36 + frames * 2));
	out += "WAVEfmt ";
	put32(out, 16);
	put16(out, 1);
	put16(out, 1);
	put32(out, rate);
	put32(out, rate * 2);
	put16(out, 2);
	put16(out, 16);
	out += "data";
	put32(out, static_cast<std::uint32_t>(frames * 2));
	for (std::uint64_t frame = 0; frame < frames; ++frame)
		out.append(wav, 44 + frame * frame_bytes + channel * 2, 2);
	return Output::success(std::move(out));
}
} // namespace bug_reporter
