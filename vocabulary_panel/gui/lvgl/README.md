# LVGL vocabulary surface

This directory owns the subordinate, two-sided vocabulary widgets beneath
`panel_ui`. The shell passes a validated `VocabularySurfaceModel` projected from
`FlashCardSession` and calls `render_vocabulary_surface(parent, model)`.

The renderer owns no LVGL lifetime, display, raw input device, semantic state,
network operation, provenance, night surface or display commitment. The shell
owns those presentation concerns, the supervisor observes raw touch, and the
canonical vocabulary reducer owns face-preserving history and study evidence.

`vocabulary_panel::lvgl_renderer` links exactly LVGL 9.5.0 and the shared
[project fonts](../../../shared/panel_fonts/README.md). The generated
font repertoire is checked by the shell; unsupported text fails explicitly.
The same widgets run in the memory renderer, SDL2 simulator and guest DRM path.

**Amended 2026-09-25:** replaced the earlier independent DRM/fbdev/evdev renderer
with shell-owned composition under ADR 0005 and Lexinet Step 7b. The production
service remains disabled and no hardware evidence is claimed.

**Amended 2026-09-25 (approved UI review):** larger type and bounded text bands
share one grid with the shell's hit geometry. The explicit Flip card control
remains alongside whole-card tapping; the duplicate instruction is removed.
The shell presents truthful review status separately from the card content.
