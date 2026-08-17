#include "panel_ui/lifecycle_snapshot_stream.hpp"
#include "panel_ui/normalized_touch_observation_stream.hpp"
#include "panel_ui/panel_intent_registry.hpp"

#include <iostream>
#include <type_traits>

int main()
{
	static_assert(std::is_abstract_v<house_panel::panel_ui::LifecycleSnapshotStream>);
	static_assert(std::is_abstract_v<house_panel::panel_ui::NormalizedTouchObservationStream>);
	const house_panel::panel_ui::PanelIntentRegistry registry;
	std::cout << "{\"data\":{\"service\":\"panel_ui\",\"status\":\"core_ready\","
		     "\"intent_definition_count\":"
		  << registry.definitions().size()
		  << "},\"error\":null,\"meta\":{\"hardware_evidence\":\"not_performed\"}}\n";
	return 0;
}
