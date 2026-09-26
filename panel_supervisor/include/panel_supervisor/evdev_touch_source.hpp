#pragma once
#include "panel_supervisor/result.hpp"
#include "bhp/touch_observation_protocol/types.h"
#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace house_panel::supervisor {
enum class TouchEvdevProtocol { SingleContact, TypeB };
struct TouchEvdevConfiguration {
	std::string path;
	std::string exact_name;
	TouchEvdevProtocol protocol;
	std::int32_t x_minimum, x_maximum, y_minimum, y_maximum;
	std::uint16_t contact_key;
	std::uint32_t slot_count;
};
struct RawTouchContact {
	bhp::touch_observation_protocol::ContactState state;
	std::int32_t x, y;
};
/** Bounded Linux event reducer, also host-testable without Linux headers.
 * Multi-contact frames cancel the gesture; they never become a single proven tap.
 * SYN_DROPPED or malformed framing fails closed; reopening requires new idle evidence.
 */
class EvdevTouchParser final {
public:
	[[nodiscard]] static Result<EvdevTouchParser> create(TouchEvdevConfiguration configuration);
	[[nodiscard]] Result<std::optional<RawTouchContact>> consume(
		std::uint16_t type, std::uint16_t code, std::int32_t value);
	/** Cancels in-flight measurement; a held contact must release before a new start. */
	void cancel_until_idle() noexcept;
private:
	explicit EvdevTouchParser(TouchEvdevConfiguration configuration);
	struct Slot { std::int32_t tracking{-1}, x{0}, y{0}; bool has_x{false}, has_y{false}; };
	TouchEvdevConfiguration configuration_;
	std::array<Slot, 32> slots_{};
	std::uint32_t selected_slot_{0};
	std::uint32_t active_slot_{0};
	bool started_{false}, cancelled_{true}, failed_{false};
	std::int32_t active_tracking_{-1};
	RawTouchContact latest_{bhp::touch_observation_protocol::ContactState::Started, 0, 0};
};
/** Sole raw input owner: exact path/name/capabilities/ranges, exclusive EVIOCGRAB.
 * drain has a fixed 256-event budget and no blocking I/O. Callback sees each SYN_REPORT
 * individually so policy cancellation cannot hide inside a accumulated contact batch.
 */
class EvdevTouchSource final {
public:
	[[nodiscard]] static Result<std::unique_ptr<EvdevTouchSource>> create(TouchEvdevConfiguration configuration);
	~EvdevTouchSource();
	EvdevTouchSource(const EvdevTouchSource &) = delete;
	EvdevTouchSource &operator=(const EvdevTouchSource &) = delete;
	[[nodiscard]] Result<void> drain(const std::function<void(const RawTouchContact &)> &consumer);
	void cancel_until_idle() noexcept { parser_.cancel_until_idle(); }
private:
	EvdevTouchSource(int descriptor, EvdevTouchParser parser);
	int descriptor_;
	EvdevTouchParser parser_;
};
}
