// bhp-media-playback-filter: high-passes raw S16_LE interleaved audio from stdin to
// stdout for the mirror unit's speaker, until the broker owns playback. For example:
//   bhp-media-playback-filter < reply.raw | aplay -D hw:CARD=L16K6Ch,DEV=0 -t raw -f S16_LE -c 2 -r 16000
// Errors go to stderr as one JSON line; the audio is never echoed there.
#include "media_broker/playback_filter.h"
#include <cstdio>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace
{
const char *kUsage = "usage: bhp-media-playback-filter [--speaker dma45-8|pui-as04008co] [--channels 2] [--rate 16000]\n"
		     "  reads S16_LE interleaved frames on stdin and writes them filtered to stdout\n";

bool parse_unsigned(const std::string &text, unsigned maximum, unsigned &value)
{
	if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos || text.size() > 6)
		return false;
	value = static_cast<unsigned>(std::stoul(text));
	return value > 0 && value <= maximum;
}

int fail(const media_broker::MediaError &error)
{
	std::cerr << nlohmann::json{{"error", {{"code", error.code}, {"message", error.message}, {"context", error.context}}}}.dump()
		  << '\n';
	return 1;
}
} // namespace

int main(int argc, char **argv)
{
	using namespace media_broker;
	static_assert(sizeof(std::int16_t) == 2);
	std::string speaker = kDefaultSpeaker;
	unsigned channels = 2, rate = 16000;
	for (int at = 1; at < argc; ++at) {
		const std::string option = argv[at];
		const bool has_value = at + 1 < argc;
		if (option == "--speaker" && has_value)
			speaker = argv[++at];
		else if (option == "--channels" && has_value && parse_unsigned(argv[++at], 32, channels))
			continue;
		else if (option == "--rate" && has_value && parse_unsigned(argv[++at], 384000, rate))
			continue;
		else {
			std::cerr << kUsage;
			return 2;
		}
	}
	const auto design = speaker_high_pass(speaker);
	if (!design)
		return fail(media_error("MEDIA_PLAYBACK.SPEAKER_UNKNOWN", "No filter for that speaker",
					"playback_filter.main"));
	auto filter = PlaybackHighPass::create(rate, channels, *design);
	if (!filter)
		return fail(filter.error());
	// The board and the development host are both little-endian, so S16_LE is native.
	const std::size_t frame_bytes = sizeof(std::int16_t) * channels;
	std::vector<std::int16_t> block(1024 * channels);
	auto *bytes = reinterpret_cast<char *>(block.data());
	std::size_t pending = 0; // bytes of a partial frame carried to the next read
	while (true) {
		const std::size_t got = std::fread(bytes + pending, 1, block.size() * sizeof(std::int16_t) - pending, stdin);
		const std::size_t frames = (pending + got) / frame_bytes;
		filter.value().process(block.data(), frames);
		if (std::fwrite(bytes, frame_bytes, frames, stdout) != frames)
			return fail(media_error("MEDIA_PLAYBACK.OUTPUT_FAILED", "Could not write the filtered audio",
						"playback_filter.main"));
		const std::size_t used = frames * frame_bytes;
		std::copy(bytes + used, bytes + pending + got, bytes);
		pending = pending + got - used;
		if (got == 0)
			break;
	}
	if (std::ferror(stdin))
		return fail(media_error("MEDIA_PLAYBACK.INPUT_FAILED", "Could not read the audio", "playback_filter.main"));
	if (pending != 0)
		return fail(media_error("MEDIA_PLAYBACK.PARTIAL_FRAME", "The input ended inside a frame",
					"playback_filter.main"));
	return std::fflush(stdout) == 0 ? 0
					: fail(media_error("MEDIA_PLAYBACK.OUTPUT_FAILED", "Could not write the filtered audio",
							   "playback_filter.main"));
}
