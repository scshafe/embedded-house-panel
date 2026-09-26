#pragma once
#include "bhp/http_runtime/http_runtime.h"
#include <cstdint>
#include <string>
namespace vocabulary_panel
{
struct HttpLexinetConfig {
	std::string lexinet_base_url;
	std::string credential_file_path;
	bhp::http_runtime::TransportBudget transport_budget;
	bool allow_insecure_http_for_tests{false};
	std::string trusted_ca_certificate_pem_for_tests;
	std::uint64_t maximum_snapshot_age_seconds{0};
	std::uint64_t allowed_clock_skew_seconds{0};
	bool clock_trusted{false};
	// Simulator-only explicit QEMU user-network host; ordinary production CA policy is
	// unchanged.
	bool allow_qemu_host_test_ca{false};
	std::int64_t current_utc_seconds{0};
};
} // namespace vocabulary_panel
