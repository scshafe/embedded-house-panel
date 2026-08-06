#pragma once

#include "vocabulary_panel/flash_card_snapshot.h"
#include "vocabulary_panel/vocabulary_panel_result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace vocabulary_panel::gui::lvgl {

enum class LvglDisplayBackend {
	drm,
	framebuffer,
};

struct LvglTouchCalibration {
	std::int32_t minimum_x{0};
	std::int32_t minimum_y{0};
	std::int32_t maximum_x{0};
	std::int32_t maximum_y{0};
	bool swap_axes{false};
};

struct LvglFlashCardRendererConfiguration {
	std::optional<LvglDisplayBackend> display_backend;
	std::string display_device_path;
	std::optional<std::int64_t> drm_connector_id;
	std::int32_t expected_horizontal_resolution{0};
	std::int32_t expected_vertical_resolution{0};
	std::string touch_device_path;
	std::optional<LvglTouchCalibration> touch_calibration;
	std::optional<std::uint32_t> night_red_rgb888;
};

enum class LvglPanelPresentationMode {
	vocabulary,
	night_red,
	blank,
};

enum class LvglFlashCardAction {
	reveal_next,
	retry,
	review_again,
	review_hard,
	review_good,
	review_easy,
};

struct LvglFlashCardActionEvent {
	LvglFlashCardAction flash_card_action;
	FlashCardStage source_flash_card_stage;
	std::uint64_t render_generation;
};

/**
 * @canonical lvgl_flash_card_renderer -- the sole LVGL FlashCardSnapshot renderer.
 *
 * This class owns LVGL's process-global state and must be initialized, serviced,
 * rendered, polled, and shut down on one thread. It returns structured domain
 * errors for configuration and hardware failures and never changes display or
 * input backends implicitly.
 */
class LvglFlashCardRenderer final {
public:
	/**
	 * Construct an inactive renderer without touching hardware.
	 *
	 * @param configuration Explicit display, connector, resolution, input,
	 * calibration, and night-color configuration. Missing values are rejected by
	 * initialize().
	 */
	explicit LvglFlashCardRenderer(LvglFlashCardRendererConfiguration configuration);
	~LvglFlashCardRenderer();

	LvglFlashCardRenderer(const LvglFlashCardRenderer &) = delete;
	LvglFlashCardRenderer &operator=(const LvglFlashCardRenderer &) = delete;
	LvglFlashCardRenderer(LvglFlashCardRenderer &&) = delete;
	LvglFlashCardRenderer &operator=(LvglFlashCardRenderer &&) = delete;

	/**
	 * Initialize LVGL and open exactly the configured display and evdev nodes.
	 *
	 * @return Success with std::monostate, or a non-retryable configuration error,
	 * a retryable device-open error, a resolution mismatch, or an LVGL global-state
	 * conflict. The initial scanout surface is black.
	 */
	VocabularyPanelResult<std::monostate> initialize();

	/**
	 * Render one immutable snapshot in vocabulary, solid-red night, or black mode.
	 *
	 * @param flash_card_snapshot Existing session presentation state. Night and
	 * blank modes intentionally do not inspect card data.
	 * @param presentation_mode Supervisor-selected presentation mode.
	 * @return The new monotonically increasing render generation, or a structured
	 * error for invalid snapshot state, unsupported glyphs, input loss, thread
	 * misuse, or an uninitialized renderer.
	 */
	VocabularyPanelResult<std::uint64_t>
	render(const FlashCardSnapshot &flash_card_snapshot,
	       LvglPanelPresentationMode presentation_mode);

	/**
	 * Run LVGL timers and input processing once on the owning thread.
	 *
	 * @return LVGL's recommended milliseconds until the next service call. The
	 * value can be UINT32_MAX when no timer is ready. Returns a structured error
	 * when input disappeared, the renderer is inactive, or thread affinity is
	 * violated.
	 */
	VocabularyPanelResult<std::uint32_t> service_once();

	/**
	 * Remove the oldest queued touch action without mutating FlashCardSession.
	 *
	 * @return Success with an optional generation-tagged action; an empty optional
	 * means no action is queued. Returns a structured lifecycle/input error when
	 * polling is unsafe.
	 */
	VocabularyPanelResult<std::optional<LvglFlashCardActionEvent>> poll_action();

	/**
	 * Close input/display resources and deinitialize LVGL on the owning thread.
	 *
	 * @return Success when shutdown completed or the renderer was already inactive;
	 * returns a thread-affinity error without altering state when called elsewhere.
	 */
	VocabularyPanelResult<std::monostate> shutdown();

	/** @return True only after successful initialize() and before shutdown(). */
	bool is_initialized() const noexcept;

private:
	class Implementation;
	std::unique_ptr<Implementation> implementation_;
};

} // namespace vocabulary_panel::gui::lvgl
