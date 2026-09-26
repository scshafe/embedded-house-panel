#pragma once
#include <array>
#include <cstdint>
namespace bhp::simulator::clock_fixtures {
inline constexpr std::array<const char *,6> invalid_texts{{"24:00","12:60","7:42","07:420","aa:bb","-1:00"}};
inline constexpr auto before_roll="07:49";
inline constexpr auto after_roll="07:50";
inline constexpr auto last_minute="23:59";
inline constexpr auto midnight="00:00";
inline constexpr auto interruption="08:00";
inline constexpr auto after_three_minutes="07:52";
inline constexpr std::array<std::uint64_t,4> transition_samples{{100,200,300,400}};
}
