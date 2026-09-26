#pragma once

#include "panel_ui/lifecycle_presentation_connection.hpp"

#include <cstdint>

namespace house_panel::panel_ui {

/**
 * @canonical lifecycle_presentation_barrier -- holds the exact lifecycle raster until
 * its acknowledgement send completes. Callers serialize all access with scene changes.
 * Rendering/promise completion alone never supplies exchange completion or readiness.
 */
class LifecyclePresentationBarrier final {
public:
	/** Holds a new, strictly increasing positive generation; false leaves state unchanged. */
	[[nodiscard]] bool hold_until_exchange_completed(std::uint64_t generation) noexcept
	{
		if (stopped_ || held_generation_ != 0 || generation <= last_held_generation_) {
			return false;
		}
		last_held_generation_ = generation;
		held_generation_ = generation;
		acknowledged_generation_ = 0;
		return true;
	}

	/**
	 * Records the canonical client's completed acknowledgement send for the held generation.
	 * A sent Failed acknowledgement releases the raster without enabling input. A transport
	 * failure must instead call disconnect(); it has no completed exchange to record.
	 */
	[[nodiscard]] bool complete_exchange(const LifecycleRenderCommit &commit) noexcept
	{
		if (stopped_ || held_generation_ == 0 ||
		    commit.render_generation != held_generation_) {
			return false;
		}
		exchange_completed_generation_ = commit.render_generation;
		acknowledged_generation_ =
			commit.presentation_result == bhp::panel_mode_protocol::PresentationResult::Committed
				? commit.render_generation
				: 0;
		held_generation_ = 0;
		return true;
	}

	/** Cancels connection-local readiness and the held raster without reusing old generations. */
	void disconnect() noexcept
	{
		held_generation_ = 0;
		exchange_completed_generation_ = 0;
		acknowledged_generation_ = 0;
	}

	/** Stops all future presentation/scene work, including late successful completions. */
	void stop() noexcept
	{
		disconnect();
		stopped_ = true;
	}

	/** A failed DRM primitive is terminal; that backend cannot establish readiness again. */
	void fail_display_commit() noexcept
	{
		display_failed_ = true;
		stop();
	}

	[[nodiscard]] bool scene_changes_allowed() const noexcept
	{
		return !stopped_ && held_generation_ == 0;
	}

	[[nodiscard]] bool input_ready(std::uint64_t current_generation) const noexcept
	{
		return scene_changes_allowed() && current_generation != 0 &&
		       acknowledged_generation_ == current_generation;
	}

	[[nodiscard]] std::uint64_t exchange_completed_generation() const noexcept
	{
		return exchange_completed_generation_;
	}

	[[nodiscard]] std::uint64_t acknowledged_generation() const noexcept
	{
		return acknowledged_generation_;
	}

	[[nodiscard]] bool display_failed() const noexcept { return display_failed_; }

private:
	std::uint64_t held_generation_{0};
	std::uint64_t last_held_generation_{0};
	std::uint64_t exchange_completed_generation_{0};
	std::uint64_t acknowledged_generation_{0};
	bool stopped_{false};
	bool display_failed_{false};
};

} // namespace house_panel::panel_ui
