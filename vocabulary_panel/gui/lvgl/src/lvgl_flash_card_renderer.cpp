#include "vocabulary_panel/gui/lvgl/lvgl_flash_card_renderer.h"

#include "lvgl_flash_card_renderer_constants.h"

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 5 || LVGL_VERSION_PATCH != 0
#error "vocabulary_panel_lvgl_renderer requires exactly LVGL 9.5.0"
#endif

namespace vocabulary_panel::gui::lvgl {
namespace {

constexpr std::size_t action_queue_capacity = 16;

const char *display_backend_name(const LvglDisplayBackend display_backend)
{
	switch (display_backend) {
	case LvglDisplayBackend::drm:
		return "drm";
	case LvglDisplayBackend::framebuffer:
		return "framebuffer";
	}
	return "invalid";
}

bool contains_only_builtin_font_glyphs(const std::string &text)
{
	for (const unsigned char character : text) {
		if (character == '\n' || character == '\t')
			continue;
		if (character < 0x20 || character > 0x7e)
			return false;
	}
	return true;
}

VocabularyPanelError make_renderer_error(
	std::string error_code, std::string error_message,
	std::map<std::string, std::string> error_context, const bool retryable)
{
	return {
		std::move(error_code),
		std::move(error_message),
		std::move(error_context),
		retryable,
	};
}

VocabularyPanelResult<std::monostate> successful_unit_result()
{
	return VocabularyPanelResult<std::monostate>::success(std::monostate{});
}

} // namespace

class LvglFlashCardRenderer::Implementation final {
public:
	explicit Implementation(LvglFlashCardRendererConfiguration configuration)
		: configuration_(std::move(configuration)),
		  action_bindings_{{{this, LvglFlashCardAction::reveal_next},
				    {this, LvglFlashCardAction::retry},
				    {this, LvglFlashCardAction::review_again},
				    {this, LvglFlashCardAction::review_hard},
				    {this, LvglFlashCardAction::review_good},
				    {this, LvglFlashCardAction::review_easy}}}
	{
	}

	~Implementation() { shutdown_unchecked(); }

	VocabularyPanelResult<std::monostate> initialize()
	{
		if (initialized_) {
			return VocabularyPanelResult<std::monostate>::failure(make_renderer_error(
				"VOCABULARY_PANEL.LVGL_ALREADY_INITIALIZED",
				"The LVGL renderer is already initialized",
				{{"operation", "initialize"}}, false));
		}
		if (const auto configuration_error = validate_configuration();
		    configuration_error.has_value()) {
			return VocabularyPanelResult<std::monostate>::failure(
				configuration_error.value());
		}
		if (lv_is_initialized()) {
			return VocabularyPanelResult<std::monostate>::failure(make_renderer_error(
				"VOCABULARY_PANEL.LVGL_GLOBAL_STATE_CONFLICT",
				"LVGL was initialized outside the canonical renderer",
				{{"operation", "initialize"}}, false));
		}

		owner_thread_id_ = std::this_thread::get_id();
		lv_init();
		owns_lvgl_state_ = true;

		const auto display_result = initialize_display();
		if (!display_result.has_value()) {
			shutdown_unchecked();
			return display_result;
		}

		const auto actual_horizontal_resolution =
			lv_display_get_horizontal_resolution(display_);
		const auto actual_vertical_resolution =
			lv_display_get_vertical_resolution(display_);
		if (actual_horizontal_resolution !=
			    configuration_.expected_horizontal_resolution ||
		    actual_vertical_resolution !=
			    configuration_.expected_vertical_resolution) {
			auto error = make_renderer_error(
				"VOCABULARY_PANEL.LVGL_DISPLAY_RESOLUTION_MISMATCH",
				"The configured display did not expose the required resolution",
				{{"operation", "initialize"},
				 {"display_device_path", configuration_.display_device_path},
				 {"expected_horizontal_resolution",
				  std::to_string(configuration_.expected_horizontal_resolution)},
				 {"expected_vertical_resolution",
				  std::to_string(configuration_.expected_vertical_resolution)},
				 {"actual_horizontal_resolution",
				  std::to_string(actual_horizontal_resolution)},
				 {"actual_vertical_resolution",
				  std::to_string(actual_vertical_resolution)}},
				false);
			shutdown_unchecked();
			return VocabularyPanelResult<std::monostate>::failure(std::move(error));
		}

		const auto input_result = initialize_input();
		if (!input_result.has_value()) {
			shutdown_unchecked();
			return input_result;
		}

		screen_ = lv_display_get_screen_active(display_);
		if (screen_ == nullptr) {
			auto error = make_renderer_error(
				"VOCABULARY_PANEL.LVGL_SCREEN_UNAVAILABLE",
				"LVGL did not create an active screen for the configured display",
				{{"operation", "initialize"},
				 {"display_device_path", configuration_.display_device_path}},
				false);
			shutdown_unchecked();
			return VocabularyPanelResult<std::monostate>::failure(std::move(error));
		}

		initialized_ = true;
		prepare_screen(constants::black_rgb888);
		lv_obj_invalidate(screen_);
		lv_refr_now(display_);
		return successful_unit_result();
	}

	VocabularyPanelResult<std::uint64_t>
	render(const FlashCardSnapshot &flash_card_snapshot,
	       const LvglPanelPresentationMode presentation_mode)
	{
		if (const auto lifecycle_error = validate_lifecycle("render", false);
		    lifecycle_error.has_value()) {
			return VocabularyPanelResult<std::uint64_t>::failure(
				lifecycle_error.value());
		}

		widget_allocation_failed_ = false;
		switch (presentation_mode) {
		case LvglPanelPresentationMode::vocabulary:
			if (input_device_lost_) {
				return VocabularyPanelResult<std::uint64_t>::failure(
					input_device_lost_error("render"));
			}
			if (const auto snapshot_error =
				    validate_flash_card_snapshot(flash_card_snapshot);
			    snapshot_error.has_value()) {
				return VocabularyPanelResult<std::uint64_t>::failure(
					snapshot_error.value());
			}
			current_flash_card_stage_ = flash_card_snapshot.flash_card_stage;
			current_presentation_mode_ = presentation_mode;
			render_vocabulary(flash_card_snapshot);
			break;
		case LvglPanelPresentationMode::night_red:
			current_flash_card_stage_ = FlashCardStage::loading;
			current_presentation_mode_ = presentation_mode;
			prepare_screen(configuration_.night_red_rgb888.value());
			break;
		case LvglPanelPresentationMode::blank:
			current_flash_card_stage_ = FlashCardStage::loading;
			current_presentation_mode_ = presentation_mode;
			prepare_screen(constants::black_rgb888);
			break;
		default:
			return VocabularyPanelResult<std::uint64_t>::failure(make_renderer_error(
				"VOCABULARY_PANEL.LVGL_PRESENTATION_MODE_INVALID",
				"The requested LVGL presentation mode is not supported",
				{{"operation", "render"}}, false));
		}

		if (widget_allocation_failed_) {
			prepare_screen(constants::black_rgb888);
			lv_obj_invalidate(screen_);
			lv_refr_now(display_);
			return VocabularyPanelResult<std::uint64_t>::failure(make_renderer_error(
				"VOCABULARY_PANEL.LVGL_WIDGET_ALLOCATION_FAILED",
				"LVGL could not allocate the complete requested screen",
				{{"operation", "render"},
				 {"flash_card_stage",
				  flash_card_stage_name(flash_card_snapshot.flash_card_stage)}},
				true));
		}

		++render_generation_;
		lv_obj_invalidate(screen_);
		lv_refr_now(display_);

		if (input_device_lost_) {
			auto error = input_device_lost_error("render");
			error.error_context["render_generation"] =
				std::to_string(render_generation_);
			return VocabularyPanelResult<std::uint64_t>::failure(std::move(error));
		}
		return VocabularyPanelResult<std::uint64_t>::success(render_generation_);
	}

	VocabularyPanelResult<std::uint32_t> service_once()
	{
		if (const auto lifecycle_error = validate_lifecycle("service_once", true);
		    lifecycle_error.has_value()) {
			return VocabularyPanelResult<std::uint32_t>::failure(
				lifecycle_error.value());
		}
		if (action_queue_overflow_) {
			return VocabularyPanelResult<std::uint32_t>::failure(make_renderer_error(
				"VOCABULARY_PANEL.LVGL_ACTION_QUEUE_OVERFLOW",
				"Touch actions arrived faster than the controller consumed them",
				{{"operation", "service_once"},
				 {"queue_capacity", std::to_string(action_queue_capacity)}},
				false));
		}

		const auto time_until_next = lv_timer_handler();
		if (input_device_lost_)
			return VocabularyPanelResult<std::uint32_t>::failure(
				input_device_lost_error("service_once"));
		if (action_queue_overflow_) {
			return VocabularyPanelResult<std::uint32_t>::failure(make_renderer_error(
				"VOCABULARY_PANEL.LVGL_ACTION_QUEUE_OVERFLOW",
				"Touch actions arrived faster than the controller consumed them",
				{{"operation", "service_once"},
				 {"queue_capacity", std::to_string(action_queue_capacity)}},
				false));
		}
		return VocabularyPanelResult<std::uint32_t>::success(time_until_next);
	}

	VocabularyPanelResult<std::optional<LvglFlashCardActionEvent>> poll_action()
	{
		if (const auto lifecycle_error = validate_lifecycle("poll_action", true);
		    lifecycle_error.has_value()) {
			return VocabularyPanelResult<std::optional<LvglFlashCardActionEvent>>::failure(
				lifecycle_error.value());
		}
		if (action_queue_overflow_) {
			return VocabularyPanelResult<std::optional<LvglFlashCardActionEvent>>::failure(
				make_renderer_error(
					"VOCABULARY_PANEL.LVGL_ACTION_QUEUE_OVERFLOW",
					"Touch actions arrived faster than the controller consumed them",
					{{"operation", "poll_action"},
					 {"queue_capacity", std::to_string(action_queue_capacity)}},
					false));
		}
		if (queued_action_count_ == 0) {
			return VocabularyPanelResult<std::optional<LvglFlashCardActionEvent>>::success(
				std::nullopt);
		}

		const auto action_event = action_queue_.at(action_queue_head_);
		action_queue_head_ = (action_queue_head_ + 1) % action_queue_capacity;
		--queued_action_count_;
		return VocabularyPanelResult<std::optional<LvglFlashCardActionEvent>>::success(
			action_event);
	}

	VocabularyPanelResult<std::monostate> shutdown()
	{
		if (!initialized_)
			return successful_unit_result();
		if (std::this_thread::get_id() != owner_thread_id_) {
			return VocabularyPanelResult<std::monostate>::failure(make_renderer_error(
				"VOCABULARY_PANEL.LVGL_THREAD_AFFINITY_VIOLATION",
				"LVGL shutdown must run on the thread that initialized the renderer",
				{{"operation", "shutdown"}}, false));
		}

		shutdown_unchecked();
		return successful_unit_result();
	}

	bool is_initialized() const noexcept { return initialized_; }

private:
	struct ActionBinding {
		Implementation *implementation;
		LvglFlashCardAction flash_card_action;
	};

	std::optional<VocabularyPanelError> validate_configuration() const
	{
		if (!configuration_.display_backend.has_value()) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_DISPLAY_BACKEND_REQUIRED",
				"An explicit LVGL display backend is required",
				{{"operation", "initialize"}}, false);
		}
		if (configuration_.display_device_path.empty() ||
		    configuration_.display_device_path.front() != '/') {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_DISPLAY_DEVICE_INVALID",
				"An absolute display device path is required",
				{{"operation", "initialize"},
				 {"display_backend",
				  display_backend_name(configuration_.display_backend.value())}},
				false);
		}
		if (configuration_.display_backend.value() == LvglDisplayBackend::drm) {
			if (!configuration_.drm_connector_id.has_value() ||
			    configuration_.drm_connector_id.value() < 0 ||
			    static_cast<std::uint64_t>(configuration_.drm_connector_id.value()) >
				    std::numeric_limits<std::uint32_t>::max()) {
				return make_renderer_error(
					"VOCABULARY_PANEL.LVGL_DRM_CONNECTOR_INVALID",
					"DRM rendering requires an explicit non-negative connector ID",
					{{"operation", "initialize"},
					 {"display_device_path",
					  configuration_.display_device_path}},
					false);
			}
		}
		else if (configuration_.drm_connector_id.has_value()) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_DRM_CONNECTOR_UNEXPECTED",
				"A framebuffer configuration must not include a DRM connector ID",
				{{"operation", "initialize"},
				 {"display_device_path", configuration_.display_device_path}},
				false);
		}
		if (configuration_.expected_horizontal_resolution <= 0 ||
		    configuration_.expected_vertical_resolution <= 0) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_EXPECTED_RESOLUTION_INVALID",
				"Positive expected horizontal and vertical resolutions are required",
				{{"operation", "initialize"}}, false);
		}
		if (configuration_.touch_device_path.empty() ||
		    configuration_.touch_device_path.front() != '/') {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_TOUCH_DEVICE_INVALID",
				"An absolute evdev touch device path is required",
				{{"operation", "initialize"}}, false);
		}
		if (!configuration_.touch_calibration.has_value()) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_TOUCH_CALIBRATION_REQUIRED",
				"Measured evdev axis calibration and swap state are required",
				{{"operation", "initialize"},
				 {"touch_device_path", configuration_.touch_device_path}},
				false);
		}
		const auto &calibration = configuration_.touch_calibration.value();
		if (calibration.minimum_x == calibration.maximum_x ||
		    calibration.minimum_y == calibration.maximum_y) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_TOUCH_CALIBRATION_INVALID",
				"Touch calibration must define non-zero X and Y ranges",
				{{"operation", "initialize"},
				 {"touch_device_path", configuration_.touch_device_path}},
				false);
		}
		if (!configuration_.night_red_rgb888.has_value() ||
		    configuration_.night_red_rgb888.value() > 0xffffff) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_NIGHT_COLOR_INVALID",
				"A measured 24-bit RGB night color is required",
				{{"operation", "initialize"}}, false);
		}
		const auto night_color = configuration_.night_red_rgb888.value();
		const auto red = (night_color >> 16) & 0xff;
		const auto green = (night_color >> 8) & 0xff;
		const auto blue = night_color & 0xff;
		if (red == 0 || red < green || red < blue) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_NIGHT_COLOR_NOT_RED_DOMINANT",
				"The calibrated night color must contain a dominant red channel",
				{{"operation", "initialize"}}, false);
		}
		return std::nullopt;
	}

	VocabularyPanelResult<std::monostate> initialize_display()
	{
		switch (configuration_.display_backend.value()) {
		case LvglDisplayBackend::drm:
#if LV_USE_LINUX_DRM
			display_ = lv_linux_drm_create();
			if (display_ == nullptr) {
				return VocabularyPanelResult<std::monostate>::failure(
					display_create_error("drm"));
			}
			lv_display_add_event_cb(display_, display_delete_event_callback,
						LV_EVENT_DELETE, this);
			if (lv_linux_drm_set_file(
				    display_, configuration_.display_device_path.c_str(),
				    configuration_.drm_connector_id.value()) != LV_RESULT_OK) {
				return VocabularyPanelResult<std::monostate>::failure(
					display_open_error("drm"));
			}
			return successful_unit_result();
#else
			return VocabularyPanelResult<std::monostate>::failure(
				display_driver_not_built_error("drm"));
#endif
		case LvglDisplayBackend::framebuffer:
#if LV_USE_LINUX_FBDEV
			display_ = lv_linux_fbdev_create();
			if (display_ == nullptr) {
				return VocabularyPanelResult<std::monostate>::failure(
					display_create_error("framebuffer"));
			}
			lv_display_add_event_cb(display_, display_delete_event_callback,
						LV_EVENT_DELETE, this);
			if (lv_linux_fbdev_set_file(
				    display_, configuration_.display_device_path.c_str()) !=
			    LV_RESULT_OK) {
				return VocabularyPanelResult<std::monostate>::failure(
					display_open_error("framebuffer"));
			}
			return successful_unit_result();
#else
			return VocabularyPanelResult<std::monostate>::failure(
				display_driver_not_built_error("framebuffer"));
#endif
		}
		return VocabularyPanelResult<std::monostate>::failure(make_renderer_error(
			"VOCABULARY_PANEL.LVGL_DISPLAY_BACKEND_INVALID",
			"The configured LVGL display backend is not supported",
			{{"operation", "initialize"}}, false));
	}

	VocabularyPanelResult<std::monostate> initialize_input()
	{
#if LV_USE_EVDEV
		touch_input_ = lv_evdev_create(LV_INDEV_TYPE_POINTER,
					     configuration_.touch_device_path.c_str());
		if (touch_input_ == nullptr) {
			return VocabularyPanelResult<std::monostate>::failure(make_renderer_error(
				"VOCABULARY_PANEL.LVGL_TOUCH_DEVICE_OPEN_FAILED",
				"LVGL could not open the configured evdev touch device",
				{{"operation", "initialize"},
				 {"touch_device_path", configuration_.touch_device_path}},
				true));
		}
		lv_indev_set_display(touch_input_, display_);
		const auto &calibration = configuration_.touch_calibration.value();
		lv_evdev_set_swap_axes(touch_input_, calibration.swap_axes);
		lv_evdev_set_calibration(touch_input_, calibration.minimum_x,
					 calibration.minimum_y, calibration.maximum_x,
					 calibration.maximum_y);
		lv_indev_add_event_cb(touch_input_, input_delete_event_callback,
				      LV_EVENT_DELETE, this);
		return successful_unit_result();
#else
		return VocabularyPanelResult<std::monostate>::failure(make_renderer_error(
			"VOCABULARY_PANEL.LVGL_EVDEV_DRIVER_NOT_BUILT",
			"The linked LVGL library does not include the required evdev driver",
			{{"operation", "initialize"},
			 {"touch_device_path", configuration_.touch_device_path}},
			false));
#endif
	}

	std::optional<VocabularyPanelError>
	validate_lifecycle(const std::string &operation, const bool require_input) const
	{
		if (!initialized_) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_NOT_INITIALIZED",
				"The LVGL renderer must be initialized before this operation",
				{{"operation", operation}}, false);
		}
		if (std::this_thread::get_id() != owner_thread_id_) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_THREAD_AFFINITY_VIOLATION",
				"All LVGL renderer operations must run on the initializing thread",
				{{"operation", operation}}, false);
		}
		if (display_device_lost_) {
			return make_renderer_error(
				"VOCABULARY_PANEL.LVGL_DISPLAY_DEVICE_LOST",
				"The configured LVGL display was deleted while the renderer was active",
				{{"operation", operation},
				 {"display_device_path", configuration_.display_device_path}},
				true);
		}
		if (require_input && input_device_lost_)
			return input_device_lost_error(operation);
		return std::nullopt;
	}

	std::optional<VocabularyPanelError>
	validate_flash_card_snapshot(const FlashCardSnapshot &snapshot) const
	{
		if (snapshot.degraded && snapshot.degraded_reason.empty()) {
			return snapshot_error(
				"VOCABULARY_PANEL.LVGL_DEGRADED_REASON_REQUIRED",
				"A degraded snapshot must identify its degraded reason", snapshot,
				"degraded_reason");
		}
		if (!snapshot.degraded && !snapshot.degraded_reason.empty()) {
			return snapshot_error(
				"VOCABULARY_PANEL.LVGL_DEGRADED_STATE_INCONSISTENT",
				"A non-degraded snapshot must not contain a degraded reason", snapshot,
				"degraded_reason");
		}
		if (!contains_only_builtin_font_glyphs(snapshot.degraded_reason)) {
			return unsupported_glyph_error(snapshot, "degraded_reason");
		}

		switch (snapshot.flash_card_stage) {
		case FlashCardStage::loading:
		case FlashCardStage::empty:
			if (snapshot.flash_card.has_value()) {
				return snapshot_error(
					"VOCABULARY_PANEL.LVGL_UNEXPECTED_CARD",
					"Loading and empty snapshots must not retain a card", snapshot,
					"flash_card");
			}
			break;
		case FlashCardStage::word:
		case FlashCardStage::definition:
		case FlashCardStage::example:
		case FlashCardStage::rating:
		case FlashCardStage::submitting:
		case FlashCardStage::offline:
		case FlashCardStage::offline_complete:
			if (!snapshot.flash_card.has_value()) {
				return snapshot_error(
					"VOCABULARY_PANEL.LVGL_CARD_REQUIRED",
					"The flash-card stage requires a validated card", snapshot,
					"flash_card");
			}
			if (const auto card_error = validate_card_text(snapshot);
			    card_error.has_value()) {
				return card_error;
			}
			break;
		case FlashCardStage::error:
			if (!snapshot.error.has_value()) {
				return snapshot_error(
					"VOCABULARY_PANEL.LVGL_ERROR_DETAIL_REQUIRED",
					"An error snapshot must retain machine-readable error detail",
					snapshot, "error");
			}
			if (!contains_only_builtin_font_glyphs(snapshot.error->error_code))
				return unsupported_glyph_error(snapshot, "error.error_code");
			if (!contains_only_builtin_font_glyphs(snapshot.error->error_message))
				return unsupported_glyph_error(snapshot, "error.error_message");
			break;
		}

		if ((snapshot.flash_card_stage == FlashCardStage::offline ||
		     snapshot.flash_card_stage == FlashCardStage::offline_complete) &&
		    !snapshot.degraded) {
			return snapshot_error(
				"VOCABULARY_PANEL.LVGL_OFFLINE_STATE_NOT_DEGRADED",
				"An offline cached card must be explicitly marked degraded", snapshot,
				"degraded");
		}
		if (snapshot.flash_card_stage == FlashCardStage::submitting &&
		    !snapshot.pending_review_rating.has_value()) {
			return snapshot_error(
				"VOCABULARY_PANEL.LVGL_PENDING_RATING_REQUIRED",
				"Submitting state must identify the pending review rating", snapshot,
				"pending_review_rating");
		}
		return std::nullopt;
	}

	std::optional<VocabularyPanelError>
	validate_card_text(const FlashCardSnapshot &snapshot) const
	{
		const auto &card = snapshot.flash_card.value();
		if (card.vocabulary_card_id.empty() || card.word.empty() ||
		    card.definition.empty() || card.example_sentence.empty()) {
			return snapshot_error(
				"VOCABULARY_PANEL.LVGL_CARD_TEXT_REQUIRED",
				"Card ID, word, definition, and example sentence are required",
				snapshot, "flash_card");
		}
		if (!contains_only_builtin_font_glyphs(card.word))
			return unsupported_glyph_error(snapshot, "flash_card.word");
		if (!contains_only_builtin_font_glyphs(card.definition))
			return unsupported_glyph_error(snapshot, "flash_card.definition");
		if (!contains_only_builtin_font_glyphs(card.example_sentence))
			return unsupported_glyph_error(snapshot,
					       "flash_card.example_sentence");
		return std::nullopt;
	}

	VocabularyPanelError snapshot_error(const std::string &error_code,
					    const std::string &error_message,
					    const FlashCardSnapshot &snapshot,
					    const std::string &field) const
	{
		return make_renderer_error(
			error_code, error_message,
			{{"operation", "render"},
			 {"field", field},
			 {"flash_card_stage", flash_card_stage_name(snapshot.flash_card_stage)}},
			false);
	}

	VocabularyPanelError
	unsupported_glyph_error(const FlashCardSnapshot &snapshot,
				const std::string &field) const
	{
		return snapshot_error(
			"VOCABULARY_PANEL.LVGL_GLYPH_SET_UNSUPPORTED",
			"The pinned built-in font cannot render one or more input glyphs",
			snapshot, field);
	}

	void render_vocabulary(const FlashCardSnapshot &snapshot)
	{
		prepare_screen(constants::day_background_rgb888);
		if (snapshot.degraded) {
			create_label("OFFLINE - " + snapshot.degraded_reason,
				     &lv_font_montserrat_14, constants::degraded_rgb888,
				     constants::outer_margin_px, 4,
				     content_width(), constants::header_height_px);
		}

		switch (snapshot.flash_card_stage) {
		case FlashCardStage::loading:
			create_status_screen("Connecting to Lexinet...",
					     constants::secondary_text_rgb888, std::nullopt);
			break;
		case FlashCardStage::word:
			create_card_action_screen(
				snapshot.flash_card->word + "\n\nTap to reveal definition",
				&lv_font_montserrat_24, LvglFlashCardAction::reveal_next);
			break;
		case FlashCardStage::definition:
			create_card_action_screen(
				snapshot.flash_card->word + "\n\n" +
					snapshot.flash_card->definition +
					"\n\nTap for an example",
				&lv_font_montserrat_20, LvglFlashCardAction::reveal_next);
			break;
		case FlashCardStage::example:
			create_card_action_screen(
				snapshot.flash_card->example_sentence +
					"\n\nTap to rate this card",
				&lv_font_montserrat_20, LvglFlashCardAction::reveal_next);
			break;
		case FlashCardStage::rating:
			create_rating_screen(snapshot.flash_card->word);
			break;
		case FlashCardStage::submitting:
			create_status_screen(
				"Saving " +
					std::string(review_rating_name(
						snapshot.pending_review_rating.value())) +
					" review to Lexinet...",
				constants::secondary_text_rgb888, std::nullopt);
			break;
		case FlashCardStage::empty:
			create_status_screen("No review card is available",
					     constants::secondary_text_rgb888,
					     LvglFlashCardAction::retry);
			break;
		case FlashCardStage::offline:
			create_card_action_screen(
				snapshot.flash_card->word +
					"\n\nCached card - reviews will not be saved\n\nTap to study",
				&lv_font_montserrat_20, LvglFlashCardAction::reveal_next);
			break;
		case FlashCardStage::offline_complete:
			create_status_screen(
				"Cached-card study complete\n\nReconnect to save reviews",
				constants::secondary_text_rgb888,
				LvglFlashCardAction::retry);
			break;
		case FlashCardStage::error:
			create_status_screen(snapshot.error->error_code + "\n\n" +
					     snapshot.error->error_message,
					     constants::error_rgb888,
					     LvglFlashCardAction::retry);
			break;
		}
	}

	void prepare_screen(const std::uint32_t background_rgb888)
	{
		lv_obj_clean(screen_);
		lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);
		lv_obj_set_style_bg_color(screen_, lv_color_hex(background_rgb888), 0);
		lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(screen_, 0, 0);
		lv_obj_set_style_pad_all(screen_, 0, 0);
	}

	void create_card_action_screen(const std::string &text, const lv_font_t *font,
				       const LvglFlashCardAction action)
	{
		const auto top = current_content_top();
		const auto height = configuration_.expected_vertical_resolution - top -
				    constants::bottom_margin_px;
		create_action_button(text, font, action, constants::card_background_rgb888,
				     constants::outer_margin_px, top, content_width(), height);
	}

	void create_status_screen(const std::string &text, const std::uint32_t text_color,
				  const std::optional<LvglFlashCardAction> action)
	{
		const auto label_height = action.has_value()
					  ? configuration_.expected_vertical_resolution -
						constants::retry_button_height_px - 50
					  : configuration_.expected_vertical_resolution - 40;
		create_label(text, &lv_font_montserrat_20, text_color,
			     constants::outer_margin_px, 18, content_width(), label_height);
		if (action.has_value()) {
			create_action_button(
				"Retry", &lv_font_montserrat_20, action.value(),
				constants::card_background_rgb888,
				(configuration_.expected_horizontal_resolution -
				 constants::retry_button_width_px) /
					2,
				configuration_.expected_vertical_resolution -
					constants::retry_button_height_px -
					constants::bottom_margin_px,
				constants::retry_button_width_px,
				constants::retry_button_height_px);
		}
	}

	void create_rating_screen(const std::string &word)
	{
		create_label("How well did you know " + word + "?",
			     &lv_font_montserrat_20, constants::primary_text_rgb888,
			     constants::outer_margin_px, 24, content_width(), 74);

		struct RatingButtonDefinition {
			const char *label;
			LvglFlashCardAction action;
			std::uint32_t background_rgb888;
		};
		const std::array<RatingButtonDefinition, 4> rating_buttons{{
			{"Again", LvglFlashCardAction::review_again, constants::again_rgb888},
			{"Hard", LvglFlashCardAction::review_hard, constants::hard_rgb888},
			{"Good", LvglFlashCardAction::review_good, constants::good_rgb888},
			{"Easy", LvglFlashCardAction::review_easy, constants::easy_rgb888},
		}};
		const auto total_gap_width =
			constants::button_gap_px *
			static_cast<std::int32_t>(rating_buttons.size() - 1);
		const auto button_width =
			(content_width() - total_gap_width) /
			static_cast<std::int32_t>(rating_buttons.size());
		const auto button_top = configuration_.expected_vertical_resolution -
					constants::rating_button_height_px -
					constants::bottom_margin_px;

		for (std::size_t index = 0; index < rating_buttons.size(); ++index) {
			const auto button_left =
				constants::outer_margin_px +
				static_cast<std::int32_t>(index) *
					(button_width + constants::button_gap_px);
			const auto width = index + 1 == rating_buttons.size()
					   ? configuration_.expected_horizontal_resolution -
						     constants::outer_margin_px - button_left
					   : button_width;
			create_action_button(
				rating_buttons.at(index).label, &lv_font_montserrat_14,
				rating_buttons.at(index).action,
				rating_buttons.at(index).background_rgb888, button_left,
				button_top, width, constants::rating_button_height_px);
		}
	}

	lv_obj_t *create_label(const std::string &text, const lv_font_t *font,
			       const std::uint32_t text_rgb888, const std::int32_t left,
			       const std::int32_t top, const std::int32_t width,
			       const std::int32_t height)
	{
		auto *label = lv_label_create(screen_);
		if (label == nullptr) {
			widget_allocation_failed_ = true;
			return nullptr;
		}
		lv_label_set_text(label, text.c_str());
		lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
		lv_obj_set_pos(label, left, top);
		lv_obj_set_size(label, width, height);
		lv_obj_set_style_text_font(label, font, 0);
		lv_obj_set_style_text_color(label, lv_color_hex(text_rgb888), 0);
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
		lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
		return label;
	}

	void create_action_button(const std::string &text, const lv_font_t *font,
				  const LvglFlashCardAction action,
				  const std::uint32_t background_rgb888,
				  const std::int32_t left, const std::int32_t top,
				  const std::int32_t width, const std::int32_t height)
	{
		auto *button = lv_button_create(screen_);
		if (button == nullptr) {
			widget_allocation_failed_ = true;
			return;
		}
		lv_obj_set_pos(button, left, top);
		lv_obj_set_size(button, width, height);
		lv_obj_set_style_bg_color(button, lv_color_hex(background_rgb888), 0);
		lv_obj_set_style_bg_color(button,
					  lv_color_hex(constants::card_pressed_rgb888),
					  LV_STATE_PRESSED);
		lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(button, 1, 0);
		lv_obj_set_style_border_color(button,
					      lv_color_hex(constants::border_rgb888), 0);
		lv_obj_set_style_radius(button, 0, 0);
		lv_obj_set_style_shadow_width(button, 0, 0);
		lv_obj_set_style_pad_all(button, constants::button_padding_px, 0);
		lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_event_cb(button, action_event_callback, LV_EVENT_CLICKED,
				    action_binding(action));

		auto *label = lv_label_create(button);
		if (label == nullptr) {
			widget_allocation_failed_ = true;
			return;
		}
		lv_label_set_text(label, text.c_str());
		lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
		lv_obj_set_width(label, width - 2 * constants::button_padding_px);
		lv_obj_set_style_text_font(label, font, 0);
		lv_obj_set_style_text_color(label,
					    lv_color_hex(constants::primary_text_rgb888), 0);
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_center(label);
	}

	ActionBinding *action_binding(const LvglFlashCardAction action)
	{
		for (auto &binding : action_bindings_) {
			if (binding.flash_card_action == action)
				return &binding;
		}
		return nullptr;
	}

	void enqueue_action(const LvglFlashCardAction action)
	{
		if (current_presentation_mode_ != LvglPanelPresentationMode::vocabulary)
			return;
		if (queued_action_count_ == action_queue_capacity) {
			action_queue_overflow_ = true;
			return;
		}
		const auto insertion_index =
			(action_queue_head_ + queued_action_count_) % action_queue_capacity;
		action_queue_.at(insertion_index) = {
			action,
			current_flash_card_stage_,
			render_generation_,
		};
		++queued_action_count_;
	}

	std::int32_t content_width() const
	{
		return configuration_.expected_horizontal_resolution -
		       2 * constants::outer_margin_px;
	}

	std::int32_t current_content_top() const
	{
		return current_presentation_mode_ ==
				       LvglPanelPresentationMode::vocabulary
		       ? constants::content_top_px
		       : constants::outer_margin_px;
	}

	VocabularyPanelError display_create_error(const std::string &backend) const
	{
		return make_renderer_error(
			"VOCABULARY_PANEL.LVGL_DISPLAY_CREATE_FAILED",
			"LVGL could not allocate the configured display driver",
			{{"operation", "initialize"},
			 {"display_backend", backend},
			 {"display_device_path", configuration_.display_device_path}},
			true);
	}

	VocabularyPanelError display_open_error(const std::string &backend) const
	{
		auto context = std::map<std::string, std::string>{
			{"operation", "initialize"},
			{"display_backend", backend},
			{"display_device_path", configuration_.display_device_path},
		};
		if (configuration_.drm_connector_id.has_value()) {
			context["drm_connector_id"] =
				std::to_string(configuration_.drm_connector_id.value());
		}
		return make_renderer_error(
			"VOCABULARY_PANEL.LVGL_DISPLAY_OPEN_FAILED",
			"LVGL could not open or configure the exact display device",
			std::move(context), true);
	}

	VocabularyPanelError display_driver_not_built_error(const std::string &backend) const
	{
		return make_renderer_error(
			"VOCABULARY_PANEL.LVGL_DISPLAY_DRIVER_NOT_BUILT",
			"The linked LVGL library does not contain the configured display driver",
			{{"operation", "initialize"},
			 {"display_backend", backend},
			 {"display_device_path", configuration_.display_device_path}},
			false);
	}

	VocabularyPanelError input_device_lost_error(const std::string &operation) const
	{
		return make_renderer_error(
			"VOCABULARY_PANEL.LVGL_TOUCH_DEVICE_LOST",
			"The configured evdev touch device disappeared while active",
			{{"operation", operation},
			 {"touch_device_path", configuration_.touch_device_path}},
			true);
	}

	void shutdown_unchecked()
	{
		if (!owns_lvgl_state_)
			return;
		shutting_down_ = true;
		lv_deinit();
		shutting_down_ = false;
		owns_lvgl_state_ = false;
		initialized_ = false;
		display_ = nullptr;
		touch_input_ = nullptr;
		screen_ = nullptr;
		input_device_lost_ = false;
		display_device_lost_ = false;
		action_queue_overflow_ = false;
		action_queue_head_ = 0;
		queued_action_count_ = 0;
	}

	static void action_event_callback(lv_event_t *event)
	{
		if (lv_event_get_code(event) != LV_EVENT_CLICKED)
			return;
		auto *binding = static_cast<ActionBinding *>(lv_event_get_user_data(event));
		if (binding == nullptr || binding->implementation == nullptr)
			return;
		binding->implementation->enqueue_action(binding->flash_card_action);
	}

	static void input_delete_event_callback(lv_event_t *event)
	{
		if (lv_event_get_code(event) != LV_EVENT_DELETE)
			return;
		auto *implementation =
			static_cast<Implementation *>(lv_event_get_user_data(event));
		if (implementation == nullptr)
			return;
		implementation->touch_input_ = nullptr;
		if (!implementation->shutting_down_)
			implementation->input_device_lost_ = true;
	}

	static void display_delete_event_callback(lv_event_t *event)
	{
		if (lv_event_get_code(event) != LV_EVENT_DELETE)
			return;
		auto *implementation =
			static_cast<Implementation *>(lv_event_get_user_data(event));
		if (implementation == nullptr)
			return;
		implementation->display_ = nullptr;
		implementation->screen_ = nullptr;
		if (!implementation->shutting_down_)
			implementation->display_device_lost_ = true;
	}

	LvglFlashCardRendererConfiguration configuration_;
	std::array<ActionBinding, 6> action_bindings_;
	std::array<LvglFlashCardActionEvent, action_queue_capacity> action_queue_{};
	std::size_t action_queue_head_{0};
	std::size_t queued_action_count_{0};
	std::uint64_t render_generation_{0};
	std::thread::id owner_thread_id_;
	lv_display_t *display_{nullptr};
	lv_indev_t *touch_input_{nullptr};
	lv_obj_t *screen_{nullptr};
	FlashCardStage current_flash_card_stage_{FlashCardStage::loading};
	LvglPanelPresentationMode current_presentation_mode_{
		LvglPanelPresentationMode::blank};
	bool owns_lvgl_state_{false};
	bool initialized_{false};
	bool shutting_down_{false};
	bool input_device_lost_{false};
	bool display_device_lost_{false};
	bool action_queue_overflow_{false};
	bool widget_allocation_failed_{false};
};

LvglFlashCardRenderer::LvglFlashCardRenderer(
	LvglFlashCardRendererConfiguration configuration)
	: implementation_(std::make_unique<Implementation>(std::move(configuration)))
{
}

LvglFlashCardRenderer::~LvglFlashCardRenderer() = default;

VocabularyPanelResult<std::monostate> LvglFlashCardRenderer::initialize()
{
	return implementation_->initialize();
}

VocabularyPanelResult<std::uint64_t>
LvglFlashCardRenderer::render(const FlashCardSnapshot &flash_card_snapshot,
			      const LvglPanelPresentationMode presentation_mode)
{
	return implementation_->render(flash_card_snapshot, presentation_mode);
}

VocabularyPanelResult<std::uint32_t> LvglFlashCardRenderer::service_once()
{
	return implementation_->service_once();
}

VocabularyPanelResult<std::optional<LvglFlashCardActionEvent>>
LvglFlashCardRenderer::poll_action()
{
	return implementation_->poll_action();
}

VocabularyPanelResult<std::monostate> LvglFlashCardRenderer::shutdown()
{
	return implementation_->shutdown();
}

bool LvglFlashCardRenderer::is_initialized() const noexcept
{
	return implementation_->is_initialized();
}

} // namespace vocabulary_panel::gui::lvgl
