#pragma once
#include "panel_ui/panel_scene.hpp"
#include "panel_ui/headless_renderer.hpp"
#include <lvgl.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace house_panel::panel_ui {
/** @canonical panel_lvgl_display -- sole LVGL lifetime and surface composition owner. */
class LvglPanelDisplay final {
public:
 explicit LvglPanelDisplay(bool sdl_window);
 ~LvglPanelDisplay();
 LvglPanelDisplay(const LvglPanelDisplay&)=delete;
 LvglPanelDisplay& operator=(const LvglPanelDisplay&)=delete;
 /** Validates all visible text before drawing one deterministic full frame. */
 std::uint64_t render(const PanelScene &scene);
 void service();
 void write_png(const std::string &path) const;
 const std::vector<std::uint32_t> &pixels() const {return pixels_;}
 const std::vector<PanelHitTarget> &hit_targets() const {return hit_targets_;}
 lv_display_t *display() const {return display_;}
 /** One drawing function per app surface; every app descriptor must have one. */
 struct SurfaceRenderer {
  const char *surface_id;
  void (LvglPanelDisplay::*draw)(lv_obj_t *screen, lv_obj_t *content, const PanelScene &scene);
 };
 static const std::vector<SurfaceRenderer> &surface_renderers();
 static const SurfaceRenderer *find_surface_renderer(const std::string &surface_id);
private:
 void draw_home(lv_obj_t *screen, lv_obj_t *content, const PanelScene &scene);
 void draw_study(lv_obj_t *screen, lv_obj_t *content, const PanelScene &scene);
 void draw_agenda(lv_obj_t *screen, lv_obj_t *content, const PanelScene &scene);
 void draw_voice_lab(lv_obj_t *screen, lv_obj_t *content, const PanelScene &scene);
 static void flush(lv_display_t *display,const lv_area_t *area,std::uint8_t *pixels);
 void draw_scene(const PanelScene &scene);
 std::unique_ptr<HeadlessRenderer> text_renderer_;
 lv_display_t *display_{nullptr};
 lv_display_flush_cb_t driver_flush_{nullptr};
 std::vector<std::uint32_t> pixels_;
 std::vector<std::uint32_t> draw_buffer_;
 std::vector<PanelHitTarget> hit_targets_;
};
}
