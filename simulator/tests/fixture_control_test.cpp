// Review Q7: the board UI's control socket refuses bad datagrams instead of
// exiting, and a socket file left by a crashed UI does not stop a restart.
#include "simulator/fixture_control.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <csignal>
#include <cstring>
#include <unistd.h>

namespace {
void require(bool condition, const char *message) {
 if (!condition) throw std::runtime_error(message);
}
sockaddr_un address_of(const std::string &path) {
 sockaddr_un address{};
 address.sun_family = AF_UNIX;
 std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
 return address;
}
void send_to(const std::string &path, const std::string &payload) {
 const int sender = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
 const auto address = address_of(path);
 require(::sendto(sender, payload.data(), payload.size(), 0, reinterpret_cast<const sockaddr *>(&address), sizeof address) ==
             static_cast<ssize_t>(payload.size()), "datagram sent");
 ::close(sender);
}
bool refuses_to_bind(const std::string &path) {
 try { bhp::simulator::FixtureControl control(path); }
 catch (const std::runtime_error &error) { return std::string(error.what()) == "SIMULATOR.CONTROL_PATH_EXISTS"; }
 return false;
}
}

int main() {
 try {
  const auto directory = std::filesystem::temp_directory_path() / ("bhp-fixture-control-" + std::to_string(::getpid()));
  std::filesystem::create_directories(directory);
  const auto path = (directory / "control.sock").string();
  {
   bhp::simulator::FixtureControl control(path);
   require(!control.receive().event && control.receive().rejected.empty(), "nothing waiting is not a refusal");
   const std::pair<std::string, const char *> refused[] = {
    {"{", bhp::simulator::FixtureControl::kFrameInvalid},
    {"[1,2]", bhp::simulator::FixtureControl::kFrameInvalid},
    {"{\"path\":\"/x\"}", bhp::simulator::FixtureControl::kFrameInvalid},
    {"{\"type\":7}", bhp::simulator::FixtureControl::kFrameInvalid},
    {"{\"type\":\"scenario\"}", bhp::simulator::FixtureControl::kFrameInvalid},
    {"{\"type\":\"screenshot\",\"path\":3}", bhp::simulator::FixtureControl::kFrameInvalid},
    {"{\"type\":\"lifecycle\",\"state\":\"day\"}", bhp::simulator::FixtureControl::kForbidden},
    {"{\"type\":\"tap\",\"x\":1,\"y\":2}", bhp::simulator::FixtureControl::kForbidden},
   };
   for (const auto &[payload, code] : refused) {
    send_to(path, payload);
    const auto frame = control.receive();
    require(!frame.event && frame.rejected == code, "a bad datagram is refused with its code");
   }
   send_to(path, "");
   require(control.receive().rejected == bhp::simulator::FixtureControl::kFrameInvalid, "an empty datagram is refused");
   send_to(path, std::string(70000, ' '));
   require(control.receive().rejected == bhp::simulator::FixtureControl::kFrameInvalid, "an oversized datagram is refused");
   send_to(path, "{\"type\":\"quit\"}");
   const auto quit = control.receive();
   require(quit.event && quit.rejected.empty() && quit.event->at("type") == "quit", "a valid request still passes");
   require(refuses_to_bind(path), "a second UI cannot take a live control socket");
  }
  require(!std::filesystem::exists(path), "a clean exit removes the socket");

  // A UI killed by a signal never runs its destructor: its socket file stays.
  // The restarted UI (systemd's Restart=) must still start, and receive.
  for (int restart = 0; restart < 2; ++restart) {
   int ready[2];
   require(::pipe(ready) == 0, "pipe");
   const pid_t ui = ::fork();
   require(ui >= 0, "fork");
   if (ui == 0) {
    ::close(ready[0]);
    try { bhp::simulator::FixtureControl control(path); (void)!::write(ready[1], "r", 1); for (;;) ::pause(); }
    catch (...) { ::_exit(3); }
   }
   ::close(ready[1]);
   char byte = 0;
   require(::read(ready[0], &byte, 1) == 1 && byte == 'r', "the UI bound its control socket");
   ::close(ready[0]);
   require(refuses_to_bind(path), "while that UI runs, a second one is refused");
   require(::kill(ui, SIGKILL) == 0, "kill");
   int status = 0;
   require(::waitpid(ui, &status, 0) == ui && WIFSIGNALED(status), "the UI was killed");
   require(std::filesystem::is_socket(path), "the killed UI's socket file remains");
  }
  {
   bhp::simulator::FixtureControl restarted(path);
   send_to(path, "{\"type\":\"quit\"}");
   require(restarted.receive().event.has_value(), "the restarted UI owns the socket again");
  }
  std::ofstream(path) << "not a socket";
  require(refuses_to_bind(path), "a path that is not a socket is never removed");
  std::filesystem::remove_all(directory);
  std::cout << "{\"ok\":true,\"test\":\"simulation_fixture_control\"}\n";
  return 0;
 } catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
 }
}
