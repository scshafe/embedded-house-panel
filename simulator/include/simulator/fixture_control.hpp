#pragma once
#include "bhp/unix_socket_path/stale_socket.h"
#include <nlohmann/json.hpp>
#include <cerrno>
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace bhp::simulator {
/** One datagram read from the control socket: an accepted event, a refusal code, or nothing waiting. */
struct ControlFrame {
 std::optional<nlohmann::json> event;
 std::string rejected;
};
/**
 * @canonical simulation_fixture_control -- the board UI's root-only fixture datagram socket.
 *
 * A malformed datagram is refused with a code and the UI keeps running (review
 * Q7). A socket file left by a UI that crashed is removed when nothing is bound
 * to it any more, so a restarted UI starts; a live one still refuses a second.
 */
class FixtureControl final {
public:
 static constexpr const char *kFrameInvalid="SIMULATOR.CONTROL_FRAME_INVALID";
 static constexpr const char *kForbidden="SIMULATOR.CONTROL_LIFECYCLE_OR_INPUT_FORBIDDEN";
 explicit FixtureControl(std::string path):path_(std::move(path)){
  if(path_.empty()||path_.front()!='/'||path_.size()>=sizeof(sockaddr_un::sun_path))throw std::runtime_error("SIMULATOR.CONTROL_PATH_INVALID");
  sockaddr_un address{};address.sun_family=AF_UNIX;std::memcpy(address.sun_path,path_.c_str(),path_.size()+1);
  // A socket left by a killed UI is removed (shared/unix_socket_path); a live
  // UI's socket, a link or any other file is left alone and refuses.
  const auto cleared=bhp::unix_socket_path::clear_stale_socket(path_);
  if(cleared!=bhp::unix_socket_path::StalePath::Absent&&cleared!=bhp::unix_socket_path::StalePath::Removed)
   throw std::runtime_error("SIMULATOR.CONTROL_PATH_EXISTS");
  fd_=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd_<0)throw std::runtime_error("SIMULATOR.CONTROL_SOCKET_FAILED");
  if(bind(fd_,reinterpret_cast<sockaddr*>(&address),sizeof address)<0||chmod(path_.c_str(),0600)<0){close(fd_);fd_=-1;throw std::runtime_error("SIMULATOR.CONTROL_BIND_FAILED");}
 }
 ~FixtureControl(){if(fd_>=0){close(fd_);unlink(path_.c_str());}}
 FixtureControl(const FixtureControl&)=delete;
 FixtureControl& operator=(const FixtureControl&)=delete;
 /** Never throws for what a sender wrote: a bad frame comes back as `rejected`. */
 ControlFrame receive(){
  char bytes[65536];
  const auto size=recv(fd_,bytes,sizeof bytes,MSG_DONTWAIT|MSG_TRUNC);
  if(size<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return {};
  if(size<=0||size>=static_cast<ssize_t>(sizeof bytes))return {std::nullopt,kFrameInvalid};
  auto event=nlohmann::json::parse(bytes,bytes+size,nullptr,false);
  if(event.is_discarded()||!event.is_object()||!event.contains("type")||!event.at("type").is_string())return {std::nullopt,kFrameInvalid};
  static const std::set<std::string> types{"scenario","clock_preview","clock_step","screenshot","quit"};
  const auto type=event.at("type").get<std::string>();
  if(!types.count(type))return {std::nullopt,kForbidden};
  if((type=="scenario"||type=="screenshot")&&(!event.contains("path")||!event.at("path").is_string()))return {std::nullopt,kFrameInvalid};
  return {std::move(event),{}};
 }
private:
 int fd_{-1};std::string path_;
};
}
