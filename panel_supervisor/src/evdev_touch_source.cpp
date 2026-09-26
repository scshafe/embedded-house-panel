#include "panel_supervisor/evdev_touch_source.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#endif

namespace house_panel::supervisor {
namespace {
// Linux UAPI event values are shared with deterministic host fixtures.
constexpr std::uint16_t kSyn = 0, kKey = 1, kAbs = 3, kReport = 0, kDropped = 3;
constexpr std::uint16_t kX = 0, kY = 1, kSlot = 47, kTracking = 57, kMtX = 53, kMtY = 54;
SupervisorError touch_error(const char *reason) {
	return {ErrorCode::EvdevCapabilityMissing, "Raw touch observation rejected",
		{{"operation", "evdev_touch_source"}, {"reason", reason}}};
}
}
EvdevTouchParser::EvdevTouchParser(TouchEvdevConfiguration configuration)
	: configuration_(std::move(configuration)) {}
Result<EvdevTouchParser> EvdevTouchParser::create(TouchEvdevConfiguration configuration) {
	if (configuration.path.empty() || configuration.path[0] != '/'
		|| configuration.path.find('\0') != std::string::npos
		|| configuration.exact_name.empty() || configuration.exact_name.size() > 255
		|| configuration.exact_name.find('\0') != std::string::npos
		|| configuration.x_minimum >= configuration.x_maximum
		|| configuration.y_minimum >= configuration.y_maximum
		|| configuration.slot_count == 0 || configuration.slot_count > 32
		|| (configuration.protocol != TouchEvdevProtocol::SingleContact && configuration.protocol != TouchEvdevProtocol::TypeB)
		|| (configuration.protocol == TouchEvdevProtocol::SingleContact
			&& (configuration.slot_count != 1 || (configuration.contact_key != 272 && configuration.contact_key != 330)))
		|| (configuration.protocol == TouchEvdevProtocol::TypeB && configuration.contact_key != 0))
		return Result<EvdevTouchParser>::failure(touch_error("configuration_invalid"));
	return Result<EvdevTouchParser>::success(EvdevTouchParser(std::move(configuration)));
}
void EvdevTouchParser::cancel_until_idle() noexcept {
	cancelled_ = std::any_of(slots_.begin(), slots_.end(), [](const Slot &slot) { return slot.tracking >= 0; });
	started_ = false;
}
Result<std::optional<RawTouchContact>> EvdevTouchParser::consume(
	std::uint16_t type, std::uint16_t code, std::int32_t value) {
	using Output = Result<std::optional<RawTouchContact>>;
	auto reject = [this](const char *reason) { failed_ = true; cancel_until_idle(); return Output::failure(touch_error(reason)); };
	if (failed_) return reject("parser_failed_reopen_required");
	if (type == kSyn && code == kDropped) return reject("synchronization_dropped");
	const bool mt = configuration_.protocol == TouchEvdevProtocol::TypeB;
	if (type == kAbs && mt && code == kSlot) {
		if (value < 0 || static_cast<std::uint32_t>(value) >= configuration_.slot_count) return reject("slot_out_of_bounds");
		selected_slot_ = static_cast<std::uint32_t>(value);
	}
	auto &slot = slots_[selected_slot_];
	if (type == kAbs && mt && code == kTracking) {
		if (value < -1) return reject("tracking_id_invalid");
		if (value >= 0 && slot.tracking >= 0 && value != slot.tracking) cancel_until_idle();
		slot.tracking = value;
	}
	if (type == kKey && !mt && code == configuration_.contact_key) {
		if (value != 0 && value != 1) return reject("contact_key_invalid");
		slot.tracking = value == 1 ? 0 : -1;
	}
	if (type == kAbs && code == (mt ? kMtX : kX)) {
		if (value < configuration_.x_minimum || value > configuration_.x_maximum) return reject("axis_x_out_of_bounds");
		slot.x = value; slot.has_x = true;
	}
	if (type == kAbs && code == (mt ? kMtY : kY)) {
		if (value < configuration_.y_minimum || value > configuration_.y_maximum) return reject("axis_y_out_of_bounds");
		slot.y = value; slot.has_y = true;
	}
	if (type != kSyn || code != kReport) return Output::success(std::nullopt);
	std::size_t count = 0; const Slot *active = nullptr; std::uint32_t active_index = 0;
	for (std::uint32_t i = 0; i < configuration_.slot_count; ++i) {
		if (slots_[i].tracking >= 0) { ++count; active = &slots_[i]; active_index = i; }
	}
	if (count > 1) { cancel_until_idle(); return Output::failure(touch_error("multiple_contacts_cancelled")); }
	if (cancelled_) { if (count == 0) cancelled_ = false; return Output::success(std::nullopt); }
	using State = bhp::touch_observation_protocol::ContactState;
	if (count == 0) {
		if (!started_) return Output::success(std::nullopt);
		started_ = false; latest_.state = State::Completed;
		latest_.x = slots_[active_slot_].x; latest_.y = slots_[active_slot_].y;
		return Output::success(latest_);
	}
	if (!active->has_x || !active->has_y) return reject("contact_axes_missing");
	if (started_ && (active_tracking_ != active->tracking || active_slot_ != active_index)) { cancel_until_idle(); return Output::failure(touch_error("contact_identity_changed")); }
	latest_ = {started_ ? State::Updated : State::Started, active->x, active->y};
	started_ = true; active_tracking_ = active->tracking; active_slot_ = active_index;
	return Output::success(latest_);
}
EvdevTouchSource::EvdevTouchSource(int descriptor, EvdevTouchParser parser)
	: descriptor_(descriptor), parser_(std::move(parser)) {}
EvdevTouchSource::~EvdevTouchSource() { ::close(descriptor_); }
Result<std::unique_ptr<EvdevTouchSource>> EvdevTouchSource::create(TouchEvdevConfiguration configuration) {
	using Output = Result<std::unique_ptr<EvdevTouchSource>>;
	auto parser = EvdevTouchParser::create(configuration);
	if (!parser) return Output::failure(parser.error());
#if defined(__linux__)
	int descriptor = ::open(configuration.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (descriptor < 0) return Output::failure(touch_error("device_open_failed"));
	auto reject = [descriptor](const char *reason) { ::close(descriptor); return Output::failure(touch_error(reason)); };
	struct stat info{}; std::array<char, 256> name{};
	if (::fstat(descriptor, &info) != 0 || !S_ISCHR(info.st_mode)
		|| ::ioctl(descriptor, EVIOCGNAME(name.size()), name.data()) <= 0
		|| std::string(name.data(), strnlen(name.data(), name.size())) != configuration.exact_name)
		return reject("device_identity_mismatch");
	const bool mt = configuration.protocol == TouchEvdevProtocol::TypeB;
	input_absinfo x{}, y{}, slots{}, tracking{};
	if (::ioctl(descriptor, EVIOCGABS(mt ? ABS_MT_POSITION_X : ABS_X), &x) != 0
		|| ::ioctl(descriptor, EVIOCGABS(mt ? ABS_MT_POSITION_Y : ABS_Y), &y) != 0
		|| x.minimum != configuration.x_minimum || x.maximum != configuration.x_maximum
		|| y.minimum != configuration.y_minimum || y.maximum != configuration.y_maximum)
		return reject("axis_configuration_mismatch");
	if (mt) {
		if (::ioctl(descriptor, EVIOCGABS(ABS_MT_SLOT), &slots) != 0 || slots.minimum != 0
			|| slots.maximum != static_cast<int>(configuration.slot_count) - 1
			|| ::ioctl(descriptor, EVIOCGABS(ABS_MT_TRACKING_ID), &tracking) != 0)
			return reject("type_b_capability_mismatch");
	} else {
		std::array<unsigned char, (KEY_MAX + 8) / 8> keys{};
		if (::ioctl(descriptor, EVIOCGBIT(EV_KEY, keys.size()), keys.data()) < 0
			|| (keys[configuration.contact_key / 8] & (1U << (configuration.contact_key % 8))) == 0)
			return reject("single_contact_key_missing");
	}
	if (::ioctl(descriptor, EVIOCGRAB, 1) != 0) return reject("exclusive_device_grab_failed");
	// Snapshot current kernel state before admitting a contact. An already held
	// finger/button must release; axis values are exact raw values, never calibrated.
	if (mt) {
		std::array<std::int32_t, 33> ids{}, xs{}, ys{};
		ids[0] = ABS_MT_TRACKING_ID; xs[0] = ABS_MT_POSITION_X; ys[0] = ABS_MT_POSITION_Y;
		const auto bytes = static_cast<int>((configuration.slot_count + 1) * sizeof(std::int32_t));
		if (::ioctl(descriptor, EVIOCGMTSLOTS(bytes), ids.data()) != 0
			|| ::ioctl(descriptor, EVIOCGMTSLOTS(bytes), xs.data()) != 0
			|| ::ioctl(descriptor, EVIOCGMTSLOTS(bytes), ys.data()) != 0) return reject("initial_slot_state_unavailable");
		for (std::uint32_t i = 0; i < configuration.slot_count; ++i) {
			if (!parser.value().consume(kAbs, kSlot, static_cast<std::int32_t>(i))
				|| !parser.value().consume(kAbs, kTracking, ids[i + 1])) return reject("initial_slot_state_invalid");
			if (ids[i + 1] >= 0 && (!parser.value().consume(kAbs, kMtX, xs[i + 1])
				|| !parser.value().consume(kAbs, kMtY, ys[i + 1]))) return reject("initial_axes_invalid");
		}
	} else {
		std::array<unsigned char, (KEY_MAX + 8) / 8> keys{};
		if (::ioctl(descriptor, EVIOCGKEY(keys.size()), keys.data()) < 0) return reject("initial_key_state_unavailable");
		const bool held = (keys[configuration.contact_key / 8] & (1U << (configuration.contact_key % 8))) != 0;
		if (!parser.value().consume(kAbs, kX, x.value) || !parser.value().consume(kAbs, kY, y.value)
			|| !parser.value().consume(kKey, configuration.contact_key, held ? 1 : 0)) return reject("initial_state_invalid");
	}
	parser.value().cancel_until_idle();
	return Output::success(std::unique_ptr<EvdevTouchSource>(new EvdevTouchSource(descriptor, std::move(parser).value())));
#else
	return Output::failure({ErrorCode::PlatformUnsupported, "Raw touch requires Linux evdev", {{"operation", "evdev_touch_source.create"}}});
#endif
}
Result<void> EvdevTouchSource::drain(const std::function<void(const RawTouchContact &)> &consumer) {
#if defined(__linux__)
	for (int i = 0; i < 256; ++i) {
		input_event event{}; auto count = ::read(descriptor_, &event, sizeof(event));
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return Result<void>::success();
		if (count < 0 && errno == EINTR) continue;
		if (count != static_cast<ssize_t>(sizeof(event))) { parser_.cancel_until_idle(); return Result<void>::failure(touch_error("device_read_failed")); }
		auto contact = parser_.consume(event.type, event.code, event.value);
		if (!contact) return Result<void>::failure(contact.error());
		if (contact.value()) consumer(*contact.value());
	}
	parser_.cancel_until_idle();
	return Result<void>::failure(touch_error("event_budget_exhausted"));
#else
	(void)consumer;
	return Result<void>::failure({ErrorCode::PlatformUnsupported, "Raw touch requires Linux evdev", {{"operation", "evdev_touch_source.drain"}}});
#endif
}
}
