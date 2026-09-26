#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace bhp::simulator {
/** Nonblocking connection to the repository's deterministic, audio-free broker. */
class FakeVoiceConnection final {
public:
 explicit FakeVoiceConnection(const std::string &repository_root);
 ~FakeVoiceConnection();
 FakeVoiceConnection(const FakeVoiceConnection&)=delete;
 FakeVoiceConnection& operator=(const FakeVoiceConnection&)=delete;
 void send(const nlohmann::json &request);
 std::optional<nlohmann::json> receive();
private:
 int pid_{-1},input_{-1},output_{-1};std::string received_;
};
}
