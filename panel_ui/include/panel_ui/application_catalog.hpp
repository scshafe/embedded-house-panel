#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace house_panel::panel_ui {
/** @extension-point panel_application_catalog -- ordered app presentation metadata.
 * Launch intents must resolve to implemented surfaces via PanelIntentRegistry.
 * Book/calendar artwork or a monogram can represent any newly registered app.
 */
struct ApplicationDescriptor {
 std::string application_id, title, subtitle, launch_intent, icon, monogram;
 std::uint32_t icon_color{0};
 bool enabled{true}, preview{false};
};
struct ApplicationCatalog {
 std::size_t columns{3}, rows{2};
 std::vector<ApplicationDescriptor> applications;
};
struct ApplicationTile {
 std::size_t application_index;
 int x, y, width, height, icon_size, icon_y, title_y, subtitle_y;
};
struct ApplicationGridPage {
 std::size_t page_count{1};
 std::vector<ApplicationTile> tiles;
};
/** Validates bounded metadata and canonical destinations; throws PANEL_UI.APP_CATALOG_*.
 * implemented_surfaces contains actual feature renderers, never arbitrary config destinations.
 */
void validate_application_catalog(const ApplicationCatalog &,const std::vector<std::string> &implemented_surfaces);
/** @canonical panel_application_grid -- shared icon/hit geometry, relative to content.
 * Catalog must be validated. Computes one page; rejects out-of-range pages.
 */
ApplicationGridPage application_grid_page(const ApplicationCatalog &,std::size_t page);
}
