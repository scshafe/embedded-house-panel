// The D-021 approval and ledger against voice-lab's pinned examples, byte for byte.
#include "golden_take.hpp"
#include "voice_lab_recorder/canonical_json.h"
#include "voice_lab_recorder/home_host.h"
#include <iostream>
#include <sys/stat.h>

using namespace voice_lab_recorder;
using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
int failures = 0;

void expect(const bool condition, const std::string &what)
{
	if (!condition) {
		++failures;
		std::cerr << "FAILED: " << what << '\n';
	}
}

std::string example_bytes(const std::string &name)
{
	return golden::read_bytes(golden::examples_root() / name);
}

ApprovalFields example_fields()
{
	const Json approval = golden::example("home-host-approval.json");
	ApprovalFields fields;
	fields.approval_id = approval["approval_id"];
	fields.endpoint = approval["endpoint"];
	fields.tailnet_suffix = approval["tailnet_suffix"];
	fields.node_stable_id = approval["node_stable_id"];
	fields.classes = approval["classes"].get<std::vector<std::string>>();
	fields.approved_at = approval["approved_at"];
	fields.expires_at = approval["expires_at"];
	fields.approved_by = approval["approved_by"];
	fields.statement = approval["statement"];
	return fields;
}

struct Scratch {
	fs::path root;
	Scratch()
	{
		std::string pattern = (fs::temp_directory_path() / "bhp-home-host-XXXXXX").string();
		root = ::mkdtemp(pattern.data());
	}
	~Scratch()
	{
		std::error_code ignored;
		fs::permissions(root, fs::perms::owner_all, fs::perm_options::add, ignored);
		fs::remove_all(root, ignored);
	}
	std::string home() const { return (root / "home-host").string(); }
};

void approval_record()
{
	const auto approval = home_host_approval(example_fields());
	expect(approval.has_value() && canonical_record(approval.value()).value() == example_bytes("home-host-approval.json"),
	       "the approval record is the example, byte for byte");
	const auto digest = record_sha256(approval.value());
	expect(digest.has_value() && digest.value() == golden::example("home-host-binding.json")["approval_sha256"],
	       "the approval's digest is the one the example binding names");

	auto fields = example_fields();
	fields.expires_at = "2027-10-20T12:00:00Z"; // 367 days
	expect(!home_host_approval(fields).has_value(), "more than 366 days is refused");
	fields = example_fields();
	fields.expires_at = "2027-10-19T12:00:00Z"; // 366 days
	expect(home_host_approval(fields).has_value(), "366 days stands");
	fields = example_fields();
	fields.endpoint = "Voice-Lab.example.ts.net";
	expect(!home_host_approval(fields).has_value(), "an upper-case endpoint is refused");
	fields = example_fields();
	fields.endpoint = "voice-lab.other.ts.net";
	expect(!home_host_approval(fields).has_value(), "an endpoint outside the tailnet is refused");
	fields = example_fields();
	fields.classes = {"measurement", "capture"};
	expect(!home_host_approval(fields).has_value(), "unsorted classes are refused");
	fields = example_fields();
	fields.node_stable_id = "n-1";
	expect(!home_host_approval(fields).has_value(), "a malformed node id is refused");
	std::int64_t seconds = 0;
	expect(!parse_utc("2026-02-31T00:00:00Z", seconds) && parse_utc("2026-10-19T12:00:00.250000Z", seconds),
	       "an impossible date is refused, a fraction is read");
}

void approve_and_load()
{
	Scratch scratch;
	HomeHost home(scratch.home());
	const auto approval = home_host_approval(example_fields()).value();
	const auto digest = record_sha256(approval).value();
	auto wrong = home.approve(approval, std::string(64, '0'), "2026-10-18T12:01:00Z");
	expect(!wrong.has_value() && wrong.error().code == "VOICE_LAB.APPROVAL_DIGEST_MISMATCH",
	       "an approval is bound only with its reviewed digest");
	auto missing = home.load("2026-10-19T00:00:00Z");
	expect(!missing.has_value() && missing.error().code == "VOICE_LAB.APPROVAL_MISSING", "nothing bound yet");
	auto binding = home.approve(approval, digest, "2026-10-18T12:01:00Z");
	expect(binding.has_value() && canonical_record(binding.value()).value() == example_bytes("home-host-binding.json"),
	       "the binding is the example, byte for byte");
	struct stat status {};
	::stat((scratch.root / "home-host/approvals/pc-home-host.json").c_str(), &status);
	expect((status.st_mode & 0777) == 0400, "the approval is stored 0400");
	auto again = home.approve(approval, digest, "2026-10-18T12:02:00Z");
	expect(!again.has_value() && again.error().code == "VOICE_LAB.APPROVAL_EXISTS", "an approval is never replaced");

	auto loaded = home.load("2026-10-19T12:00:00Z");
	expect(loaded.has_value() && loaded.value().sha256 == digest, "the bound approval loads in force");
	auto expired = home.load("2027-08-15T12:00:00Z");
	expect(!expired.has_value() && expired.error().code == "VOICE_LAB.APPROVAL_NOT_IN_FORCE",
	       "an approval ends at its expiry");

	const auto path = scratch.root / "home-host/approvals/pc-home-host.json";
	fs::permissions(path, fs::perms::owner_write, fs::perm_options::add);
	Json changed = approval;
	changed["node_stable_id"] = "nOTHER";
	golden::write_bytes(path, canonical_record(changed).value());
	auto tampered = home.load("2026-10-19T12:00:00Z");
	expect(!tampered.has_value() && tampered.error().code == "VOICE_LAB.APPROVAL_TAMPERED",
	       "an approval file that is not the bound one is refused");
}

void ledger()
{
	Scratch scratch;
	HomeHost home(scratch.home());
	const auto approval = home_host_approval(example_fields()).value();
	const BoundApproval bound{approval, record_sha256(approval).value()};
	const Json manifest = golden::example("take/manifest.json");
	const std::string take_id = manifest["take_id"];
	const std::string manifest_sha256 = golden::example("take/READY")["manifest_sha256"];
	auto started = home.append(bound, {"upload-started", "2026-10-19T12:00:06Z", take_id, manifest_sha256, {}, {}});
	expect(started.has_value() &&
		       canonical_record(started.value()).value() == example_bytes("home-host-ledger-upload-started.json"),
	       "upload-started is the example, byte for byte");
	auto without_receipt =
		home.append(bound, {"upload-completed", "2026-10-19T12:01:00Z", take_id, manifest_sha256, {}, {}});
	expect(!without_receipt.has_value() && without_receipt.error().code == "VOICE_LAB.LEDGER_ENTRY_INVALID",
	       "a completed upload names its receipt");
	auto completed = home.append(
		bound, {"upload-completed", "2026-10-19T12:01:00Z", take_id, manifest_sha256, std::string(64, 'a'), {}});
	auto entries = home.entries();
	expect(completed.has_value() && entries.has_value() && entries.value().size() == 2 &&
		       completed.value()["previous_sha256"] == entries.value()[0].second &&
		       completed.value()["sequence"] == 2,
	       "the second entry links to the first");
	fs::remove(scratch.root / "home-host/ledger/00000001.json");
	auto broken = home.entries();
	expect(!broken.has_value() && broken.error().code == "VOICE_LAB.LEDGER_BROKEN", "a gap breaks the ledger");
	auto refused = home.append(bound, {"upload-started", "2026-10-19T12:02:00Z", take_id, manifest_sha256, {}, {}});
	expect(!refused.has_value(), "nothing is appended to a broken ledger");
}

void peers()
{
	const auto approval = home_host_approval(example_fields()).value();
	expect(check_peer(approval, {"nEXAMPLE11CNTRL", {"tag:voice-lab"}, "voice-lab.example.ts.net."}).has_value(),
	       "the approved node");
	expect(!check_peer(approval, {"nOTHER", {"tag:voice-lab"}, "voice-lab.example.ts.net."}).has_value(),
	       "another node is refused");
	expect(!check_peer(approval, {"nEXAMPLE11CNTRL", {"tag:other"}, "voice-lab.example.ts.net."}).has_value(),
	       "a node without tag:voice-lab is refused");
	expect(!check_peer(approval, {"nEXAMPLE11CNTRL", {"tag:voice-lab"}, "other.example.ts.net."}).has_value(),
	       "a node with another name is refused");
}
} // namespace

int main()
{
	approval_record();
	approve_and_load();
	ledger();
	peers();
	if (failures)
		std::cerr << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
