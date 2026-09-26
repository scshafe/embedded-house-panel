#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace house_panel::panel_ui {
/** @canonical drm_page_flip_display_commit -- DRM dumb-buffer page-flip completion.
 * Opens only an explicitly supplied card and connector; requires 1024x600.
 * commit returns true only after the matching kernel page-flip event arrives.
 * This backend is opt-in; it supplies simulation evidence, never FRDM evidence.
 */
class DrmPageFlipDisplay final {
public:
 DrmPageFlipDisplay(const std::string &card,std::uint32_t connector);
 ~DrmPageFlipDisplay();
 DrmPageFlipDisplay(const DrmPageFlipDisplay&)=delete;
 DrmPageFlipDisplay& operator=(const DrmPageFlipDisplay&)=delete;
 bool commit(const std::vector<std::uint32_t> &pixels);
private:
 struct Implementation;
 std::unique_ptr<Implementation> implementation_;
};
}
