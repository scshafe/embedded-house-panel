#pragma once
#include "panel_ui/application_catalog.hpp"
#include <string>
namespace bhp::simulator {
/** Reads a bounded, closed startup catalog; malformed input throws SIMULATOR.APP_CATALOG_*.
 * Canonical registry routes must target an implemented simulation feature surface.
 */
house_panel::panel_ui::ApplicationCatalog read_application_catalog(const std::string &path);
}
