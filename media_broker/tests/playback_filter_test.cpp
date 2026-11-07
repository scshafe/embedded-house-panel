#include "media_broker/playback_filter.h"
#include <cmath>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>
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

constexpr unsigned kRate = 16000; // the XVF3800's playback rate

/** The filter's gain in dB for a steady tone, from the RMS of its second half second. */
double gain_db(const HighPassDesign design, const double hz, const double amplitude = 10000.0)
{
	auto filter = PlaybackHighPass::create(kRate, 1, design);
	std::vector<std::int16_t> samples(kRate);
	for (std::size_t at = 0; at < samples.size(); ++at)
		samples[at] = static_cast<std::int16_t>(std::lround(amplitude * std::sin(2.0 * M_PI * hz * at / kRate)));
	std::vector<std::int16_t> input = samples;
	filter.value().process(samples.data(), samples.size());
	double in = 0.0, out = 0.0;
	for (std::size_t at = kRate / 2; at < samples.size(); ++at) {
		in += double(input[at]) * input[at];
		out += double(samples[at]) * samples[at];
	}
	return 10.0 * std::log10(out / in);
}

void test_designs()
{
	expect(speaker_high_pass(kDefaultSpeaker).has_value(), "the default speaker has a design");
	expect(speaker_high_pass("dma45-8")->corner_hz == 150.0, "dma45-8 corner");
	expect(speaker_high_pass("pui-as04008co")->corner_hz == 500.0, "pui corner");
	expect(!speaker_high_pass("unknown").has_value(), "an unknown speaker has none");
	expect(!speaker_high_pass("").has_value(), "an empty name has none");
}

void test_invalid_parameters()
{
	const HighPassDesign good{150.0, M_SQRT1_2};
	for (const auto &[rate, channels, design] :
	     std::vector<std::tuple<unsigned, unsigned, HighPassDesign>>{{0, 2, good},
									 {kRate, 0, good},
									 {kRate, 33, good},
									 {kRate, 2, {0.0, M_SQRT1_2}},
									 {kRate, 2, {8000.0, M_SQRT1_2}},
									 {kRate, 2, {-1.0, M_SQRT1_2}},
									 {kRate, 2, {150.0, 0.0}},
									 {kRate, 2, {150.0, 11.0}},
									 {kRate, 2, {NAN, M_SQRT1_2}}}) {
		const auto filter = PlaybackHighPass::create(rate, channels, design);
		expect(!filter && filter.error().code == "MEDIA_PLAYBACK.FILTER_INVALID", "rejects a bad design");
	}
	expect(PlaybackHighPass::create(kRate, 2, good).has_value(), "accepts the XVF3800's format");
}

void test_dma45_response()
{
	const auto design = *speaker_high_pass("dma45-8");
	expect(std::abs(gain_db(design, 150.0) + 3.01) < 0.1, "-3 dB at the corner");
	expect(std::abs(gain_db(design, 1000.0)) < 0.1, "flat at 1 kHz");
	expect(std::abs(gain_db(design, 300.0)) < 0.35, "within 0.35 dB at 300 Hz");
	expect(gain_db(design, 60.0) < -15.0, "at least 15 dB down at 60 Hz");
	expect(gain_db(design, 30.0) < -27.0, "at least 27 dB down at 30 Hz");
}

void test_pui_response()
{
	const auto design = *speaker_high_pass("pui-as04008co");
	expect(std::abs(gain_db(design, 500.0) + 3.01) < 0.1, "-3 dB at the PUI's corner");
	expect(gain_db(design, 200.0) < -14.0, "at least 14 dB down at 200 Hz");
}

void test_dc_and_saturation()
{
	auto filter = PlaybackHighPass::create(kRate, 1, *speaker_high_pass("dma45-8"));
	std::vector<std::int16_t> samples(kRate, 20000);
	filter.value().process(samples.data(), samples.size());
	expect(std::abs(samples.back()) <= 1, "removes DC");
	// A full-scale step overshoots; it must clip, never wrap round.
	std::vector<std::int16_t> step(64, -32768);
	step.resize(128, 32767);
	filter.value().reset();
	filter.value().process(step.data(), step.size());
	expect(step[64] == 32767, "a full-scale step saturates");
	for (std::size_t at = 64; at < 70; ++at)
		expect(step[at] > 0, "no wrap after the step");
}

void test_state_and_channels()
{
	const auto design = *speaker_high_pass("dma45-8");
	std::vector<std::int16_t> whole(2 * 1000), split;
	for (std::size_t frame = 0; frame < 1000; ++frame)
		whole[2 * frame] = static_cast<std::int16_t>(std::lround(8000.0 * std::sin(2.0 * M_PI * 90.0 * frame / kRate)));
	split = whole;
	auto one = PlaybackHighPass::create(kRate, 2, design);
	one.value().process(whole.data(), 1000);
	auto two = PlaybackHighPass::create(kRate, 2, design);
	two.value().process(split.data(), 333);
	two.value().process(split.data() + 2 * 333, 1000 - 333);
	expect(whole == split, "the state carries across calls");
	bool right_silent = true;
	for (std::size_t frame = 0; frame < 1000; ++frame)
		right_silent = right_silent && whole[2 * frame + 1] == 0;
	expect(right_silent, "channels are independent");
}
} // namespace

int main()
{
	test_designs();
	test_invalid_parameters();
	test_dma45_response();
	test_pui_response();
	test_dc_and_saturation();
	test_state_and_channels();
	std::cout << "playback_filter: " << failures << " failures\n";
	return failures ? 1 : 0;
}
