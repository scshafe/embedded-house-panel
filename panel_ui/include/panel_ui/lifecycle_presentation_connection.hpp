#pragma once

#include "panel_ui/lifecycle_snapshot_stream.hpp"
#include "panel_ui/result.hpp"

#include "bhp/panel_mode_protocol/types.h"

#include <chrono>
#include <cstdint>
#include <functional>

namespace house_panel::panel_ui {

/** Explicit result observed from one renderer attempt for one typed snapshot. */
struct LifecycleRenderCommit {
	std::uint64_t render_generation;
	bhp::panel_mode_protocol::PresentationResult presentation_result;
};

/**
 * Renders the supplied typed snapshot and reports the strongest observed commit result.
 *
 * `committed` may be returned only after the selected display commit primitive
 * completes. `failed` identifies an attempted generation that did not commit.
 * Returning an error supplies no generation that can safely be acknowledged.
 */
using LifecycleRenderCommitCallback = std::function<PanelUiResult<LifecycleRenderCommit>(
	const bhp::panel_mode_protocol::Snapshot &snapshot)>;

// @extension-point lifecycle_presentation_connection -- authenticated lifecycle input plus
// acknowledgement-only renderer outcome output; never feature traffic.
class LifecyclePresentationConnection : public LifecycleSnapshotStream {
public:
	~LifecyclePresentationConnection() override = default;

	[[nodiscard]] virtual PanelUiResult<LifecycleRenderCommit>
	receive_render_and_acknowledge_until(
		std::chrono::steady_clock::time_point deadline,
		const LifecycleRenderCommitCallback &render_and_commit) = 0;

	virtual void close() noexcept = 0;
	[[nodiscard]] virtual bool connected() const noexcept = 0;
};

} // namespace house_panel::panel_ui
