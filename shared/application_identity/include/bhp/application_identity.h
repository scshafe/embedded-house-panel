#pragma once

#include <string_view>

#ifndef BHP_APPLIANCE_RELEASE_ID
#error "BHP_APPLIANCE_RELEASE_ID must be supplied by the top-level build"
#endif

namespace bhp {

// @canonical application_identity -- one build-stamped identity for the application bundle.
inline constexpr std::string_view kApplianceReleaseId = BHP_APPLIANCE_RELEASE_ID;

static_assert(!kApplianceReleaseId.empty(), "appliance release identity must not be empty");
static_assert(kApplianceReleaseId.size() <= 128,
	      "appliance release identity must fit the protocol bound");

} // namespace bhp
