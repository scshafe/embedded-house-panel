#include "panel_supervisor/touch_observation_runtime.hpp"
#include <openssl/rand.h>
#include <array>

namespace house_panel::supervisor {
namespace {
namespace touch = bhp::touch_observation_protocol;
SupervisorError observation_error(const touch::Error &error) {
	auto context = error.context;
	context["touch_error_code"] = std::string(touch::to_string(error.code));
	return {ErrorCode::AdapterUnavailable, "Touch observation delivery is degraded", std::move(context)};
}
Result<std::string> random_identity() {
	std::array<unsigned char, 16> bytes{};
	if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
		return Result<std::string>::failure({ErrorCode::UiSocketRandomFailed,
			"Touch observation entropy unavailable", {{"operation", "touch_runtime.random_identity"}}});
	constexpr char hex[] = "0123456789abcdef";
	std::string id; id.reserve(32);
	for (auto byte : bytes) { id += hex[byte >> 4]; id += hex[byte & 15]; }
	return Result<std::string>::success(std::move(id));
}
}
TouchObservationRuntime::TouchObservationRuntime(UnixSocketUiModeNotifier &lifecycle,
	std::unique_ptr<EvdevTouchSource> source, std::unique_ptr<touch::UnixTouchObservationServer> server,
	std::unique_ptr<TouchObservationAuthority> authority, std::unique_ptr<MediaWindowEndpoint> media_window)
	: lifecycle_(lifecycle), source_(std::move(source)), server_(std::move(server)), authority_(std::move(authority)),
	  media_window_(std::move(media_window)) {}
Result<std::unique_ptr<TouchObservationRuntime>> TouchObservationRuntime::create(
	TouchRuntimeConfiguration configuration, std::uint32_t expected_ui_uid, UnixSocketUiModeNotifier &lifecycle,
	std::unique_ptr<MediaWindowEndpoint> media_window) {
	using Output = Result<std::unique_ptr<TouchObservationRuntime>>;
	auto authority = TouchObservationAuthority::create(configuration.authority, random_identity,
		[] { return std::chrono::steady_clock::now(); });
	if (!authority) return Output::failure(authority.error());
	auto source = EvdevTouchSource::create(configuration.device);
	if (!source) return Output::failure(source.error());
	auto server = touch::UnixTouchObservationServer::create(configuration.socket_path, expected_ui_uid);
	if (!server) return Output::failure(observation_error(server.error()));
	return Output::success(std::unique_ptr<TouchObservationRuntime>(new TouchObservationRuntime(lifecycle,
		std::move(source).value(), std::move(server).value(), std::move(authority).value(),
		std::move(media_window))));
}
void TouchObservationRuntime::invalidate(TouchObservationInvalidation reason) noexcept {
	authority_->invalidate(reason); source_->cancel_until_idle(); binding_.reset(); contact_active_ = false;
	forwarding_id_.clear();
}
Result<PendingPresentation> TouchObservationRuntime::publish(const UiModeNotification &notification) {
	// Runs before the lifecycle publication and therefore before any suspend path.
	auto reason = TouchObservationInvalidation::NewLifecycleSnapshot;
	if (notification.system_state != SystemState::Active) reason = TouchObservationInvalidation::SuspendPreparation;
	else if (notification.solar_state != SolarState::Day) reason = TouchObservationInvalidation::Night;
	else if (notification.presence_state != PresenceState::Present) reason = TouchObservationInvalidation::Absence;
	invalidate(reason);
	eligible_ = notification.system_state == SystemState::Active
		&& notification.solar_state == SolarState::Day && notification.presence_state == PresenceState::Present;
	// The media window follows the same eligibility and closes before the lifecycle
	// publication, so a broker stops capture before any suspend path runs.
	if (media_window_) {
		using Window = bhp::media_window_protocol::WindowReason;
		const auto window_reason = eligible_ ? Window::DayPresent
			: notification.system_state != SystemState::Active ? Window::SuspendPreparation
			: notification.solar_state != SolarState::Day ? Window::Night : Window::Absence;
		media_window_->set_window(eligible_, window_reason);
	}
	return lifecycle_.publish(notification);
}
PresentationOutcome TouchObservationRuntime::await_presentation(const PendingPresentation &pending,
	std::chrono::steady_clock::time_point deadline) {
	auto outcome = lifecycle_.await_presentation(pending, deadline);
	// Events queued while display commitment was pending cannot attest the new frame.
	if (!source_failed_) {
		auto drained = source_->drain([](const RawTouchContact &) {});
		if (!drained) { source_failed_ = true; pending_error_ = drained.error(); }
		source_->cancel_until_idle();
	}
	const auto *committed = std::get_if<PresentationCommitted>(&outcome);
	if (eligible_ && committed && !source_failed_) {
		TouchPresentationBinding binding{2, committed->supervisor_session_id,
			committed->publication_sequence, committed->snapshot_id,
			committed->connection_challenge, committed->render_generation};
		auto recorded = authority_->record_committed_presentation(binding);
		if (recorded) binding_ = std::move(binding);
		else pending_error_ = recorded.error();
	}
	return outcome;
}
Result<void> TouchObservationRuntime::observe(const RawTouchContact &contact) {
	if (!binding_ || !server_->connected()) return Result<void>::success();
	using State = touch::ContactState;
	std::uint64_t duration = 0;
	if (contact.state == State::Started) {
		auto started = authority_->record_contact_started();
		if (!started) return started;
		auto id = random_identity(); if (!id) return Result<void>::failure(id.error());
		// Forwarding IDs before completion have never been registered for redemption.
		forwarding_id_ = std::move(id).value(); contact_active_ = true;
	} else if (!contact_active_) return Result<void>::success();
	if (contact.state == State::Completed) {
		auto receipt = authority_->record_contact_completed(); contact_active_ = false;
		if (!receipt) return Result<void>::failure(receipt.error());
		forwarding_id_ = receipt.value().touch_observation_id;
		duration = static_cast<std::uint64_t>(receipt.value().supervisor_measured_contact_duration.count());
	}
	if (sequence_ >= bhp::panel_mode_protocol::kMaximumSafeJsonInteger)
		return Result<void>::failure({ErrorCode::RevisionOverflow, "Touch sequence exhausted", {{"operation", "touch_runtime.observe"}}});
	touch::NormalizedTouchObservation record{1, forwarding_id_, ++sequence_, *binding_, contact.x, contact.y, contact.state, duration};
	auto sent = server_->try_publish(record);
	if (!sent) return Result<void>::failure(observation_error(sent.error()));
	return Result<void>::success();
}
Result<void> TouchObservationRuntime::poll_observations() {
	auto touch = poll_touch();
	if (!media_window_) return touch;
	auto window = media_window_->pump([this](const TouchObservationClaim &claim) {
		return authority_->redeem_for_media_broker(claim);
	});
	return touch ? window : touch;
}
Result<void> TouchObservationRuntime::poll_touch() {
	if (pending_error_) {
		auto error = *pending_error_; pending_error_.reset();
		return Result<void>::failure(std::move(error));
	}
	if (!lifecycle_.observe_client_liveness()) invalidate(TouchObservationInvalidation::UiDisconnected);
	auto transport = server_->pump();
	if (!transport) { invalidate(TouchObservationInvalidation::UiDisconnected); return Result<void>::failure(observation_error(transport.error())); }
	if (source_failed_) return Result<void>::success();
	std::optional<SupervisorError> delivery_error;
	auto drained = source_->drain([&](const RawTouchContact &contact) {
		if (delivery_error) return;
		auto result = observe(contact);
		if (!result) delivery_error = result.error();
	});
	if (!drained) {
		invalidate(TouchObservationInvalidation::PresentationMismatch);
		auto reason = drained.error().context().find("reason");
		if (reason == drained.error().context().end()
			|| (reason->second != "multiple_contacts_cancelled" && reason->second != "contact_identity_changed")) source_failed_ = true;
		return drained;
	}
	if (delivery_error) {
		server_->disconnect(); invalidate(TouchObservationInvalidation::UiDisconnected);
		return Result<void>::failure(*delivery_error);
	}
	return Result<void>::success();
}
}
