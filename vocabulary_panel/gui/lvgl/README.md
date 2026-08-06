# LVGL flash-card renderer

This directory owns the LVGL 9.5.0 implementation of the
`@extension-point flash_card_renderer`. It renders an existing
`FlashCardSnapshot`; it does not fetch cards, mutate `FlashCardSession`, choose
day/night/suspend policy, control the backlight, or silently select hardware.

## Public interface

`LvglFlashCardRenderer` has one explicit lifecycle:

1. Construct it with a complete `LvglFlashCardRendererConfiguration`.
2. Call `initialize()` once on the future UI/event thread. Initialization opens
   exactly the named DRM+connector or fbdev node and exactly the named evdev
   node, verifies the measured resolution, applies the supplied calibration,
   and paints an initial black surface.
3. Call `render()` with a `FlashCardSnapshot` and supervisor presentation mode.
4. Repeatedly call `service_once()` and `poll_action()` on that same thread.
   Actions carry the render generation and source stage; the controller must
   reject an action if it no longer matches its current model generation.
5. Call `shutdown()` on the same thread. Blank mode paints black but does not
   itself release hardware; the controller may shut down after the supervisor
   has sequenced backlight/power policy.

Night mode is a separate solid-color path. Its red RGB value is required
configuration because it must come from dark-room hardware calibration. Blank
mode and initialization are black, so this implementation does not introduce a
white intermediate screen.

The pinned built-in fonts cover printable ASCII only. Rendering other card or
error text returns `VOCABULARY_PANEL.LVGL_GLYPH_SET_UNSUPPORTED` rather than
showing plausible replacement glyphs. Add a measured/generated project font in
a later, explicit change when the Lexinet character repertoire is known.

## Root CMake wiring

The host build must remain independent of LVGL and libdrm. Root owns these
changes outside this directory:

```cmake
option(BHP_BUILD_LVGL_RENDERER "Build the LVGL target renderer" OFF)

# In vocabulary_panel/CMakeLists.txt, after vocabulary_panel_core exists:
if(BHP_BUILD_LVGL_RENDERER)
  add_subdirectory(gui/lvgl)
endif()
```

The enabled target is `vocabulary_panel_lvgl_renderer`, with alias
`vocabulary_panel::lvgl_renderer`. Link the future target-only controller or
executable to the alias. The nested build accepts either an existing
`lvgl::lvgl` target or the exact `lvgl = 9.5.0` pkg-config package and always
requires the `libdrm` pkg-config package. It installs
`libvocabulary_panel_lvgl_renderer.a` and the public header tree when enabled.

## Buildroot wiring

The external tree root must source this package and include external package
makefiles:

```make
source "$BR2_EXTERNAL_BHP_PATH/package/lvgl/Config.in"
include $(sort $(wildcard $(BR2_EXTERNAL_BHP_PATH)/package/*/*.mk))
```

The application package must `select BR2_PACKAGE_LVGL`, add `lvgl` to its
dependencies, and configure with `-DBHP_BUILD_LVGL_RENDERER=ON`. The selected
LVGL package enables only software rendering plus direct DRM dumb buffers,
fbdev, and evdev. It deliberately disables GBM, EGL/OpenGL ES, libinput,
Wayland, X11, SDL, examples, demos, and internal ThorVG.

Do not put guessed device paths, connector IDs, touch ranges, axis transforms,
or night colors in the Buildroot package. The target controller must load
validated configuration derived from `hardware_tests/results/` and pass it to
the renderer. Example values in tests must be marked fixtures and must not be
promoted to production configuration.
