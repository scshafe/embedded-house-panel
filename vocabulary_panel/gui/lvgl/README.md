# LVGL vocabulary surface

This directory owns the subordinate, two-sided vocabulary widgets beneath
`panel_ui`. The shell passes a validated `VocabularySurfaceModel` projected from
`FlashCardSession` and calls `render_vocabulary_surface(parent, model)`.

The renderer owns no LVGL lifetime, display, raw input device, semantic state,
network operation, provenance, night surface or display commitment. The shell
owns those presentation concerns, the supervisor observes raw touch, and the
canonical vocabulary reducer owns face-preserving history and study evidence.

`vocabulary_panel::lvgl_renderer` links exactly LVGL 9.5.0. The pinned built-in
font repertoire is checked by the shell; unsupported text fails explicitly.
The same widgets run in the memory renderer, SDL2 simulator and guest DRM path.

**Amended 2026-09-25:** replaced the earlier independent DRM/fbdev/evdev renderer
with shell-owned composition under ADR 0005 and Lexinet Step 7b. The production
service remains disabled and no hardware evidence is claimed.
