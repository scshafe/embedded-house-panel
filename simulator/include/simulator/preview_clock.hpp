#pragma once
#include <cstdint>
#include <string>

namespace bhp::simulator {
struct PreviewClockFrame {
 std::string text;
 std::string previous_text;
 std::uint16_t transition_milli{1000};
 bool operator==(const PreviewClockFrame &other) const noexcept {
  return text==other.text && previous_text==other.previous_text && transition_milli==other.transition_milli;
 }
};
/** @canonical simulation_preview_clock -- explicit fixture time, never household time.
 * Monotonic samples advance a configured HH:MM once per minute. No timezone,
 * wall-clock or network inference. Reversed samples and malformed time fail.
 */
class PreviewClock final {
public:
 static constexpr std::uint64_t transition_duration_ms=400;
 static bool valid_text(const std::string &text) noexcept;
 static std::string next_minute(const std::string &text);
 /** Configure the fixture origin. Throws SIMULATOR.CLOCK_TEXT_INVALID on bad HH:MM. */
 void reset(const std::string &text,std::uint64_t now_ms,bool ticking);
 /** Start one local preview change; no command or lifecycle authority is implied. */
 void change_to(const std::string &text,std::uint64_t now_ms);
 /** Sample a deterministic transition. A hidden/reappearing clock never replays motion. */
 PreviewClockFrame sample(std::uint64_t now_ms,bool visible=true);
 void cancel_transition() noexcept;
 const PreviewClockFrame &frame() const noexcept {return frame_;}
private:
 PreviewClockFrame frame_{"07:42",{},1000};
 std::uint64_t origin_ms_{0},last_ms_{0},transition_ms_{0};
 unsigned origin_minute_{462};
 bool ticking_{false},visible_{true};
};
}
