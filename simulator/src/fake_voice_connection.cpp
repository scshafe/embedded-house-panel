#include "simulator/fake_voice_connection.hpp"
#include <spawn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdexcept>
#include <cerrno>
#include <initializer_list>
extern char **environ;
namespace bhp::simulator {
FakeVoiceConnection::FakeVoiceConnection(const std::string &root){
 signal(SIGPIPE,SIG_IGN);
 int in[2],out[2];if(pipe(in)||pipe(out))throw std::runtime_error("SIMULATOR.BROKER_PIPE_FAILED");
 posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
 posix_spawn_file_actions_adddup2(&actions,in[0],STDIN_FILENO);posix_spawn_file_actions_adddup2(&actions,out[1],STDOUT_FILENO);
 for(int fd:{in[0],in[1],out[0],out[1]})posix_spawn_file_actions_addclose(&actions,fd);
 std::string program="python3",script=root+"/media_broker/fake_media_broker.py",option="--root";
 char *argv[]{program.data(),script.data(),option.data(),const_cast<char*>(root.c_str()),nullptr};pid_t child=-1;
 int result=posix_spawnp(&child,"python3",&actions,nullptr,argv,environ);posix_spawn_file_actions_destroy(&actions);close(in[0]);close(out[1]);
 if(result){close(in[1]);close(out[0]);throw std::runtime_error("SIMULATOR.BROKER_SPAWN_FAILED");}
 pid_=child;input_=in[1];output_=out[0];fcntl(input_,F_SETFL,O_NONBLOCK);fcntl(output_,F_SETFL,O_NONBLOCK);
}
FakeVoiceConnection::~FakeVoiceConnection(){if(input_>=0)close(input_);if(output_>=0)close(output_);if(pid_>0){kill(pid_,SIGTERM);waitpid(pid_,nullptr,0);}}
void FakeVoiceConnection::send(const nlohmann::json &request){auto bytes=request.dump()+"\n";if(bytes.size()>4096)throw std::runtime_error("SIMULATOR.BROKER_FRAME_TOO_LARGE");auto count=write(input_,bytes.data(),bytes.size());if(count!=static_cast<ssize_t>(bytes.size()))throw std::runtime_error("SIMULATOR.BROKER_BACKPRESSURE");}
std::optional<nlohmann::json> FakeVoiceConnection::receive(){
 auto delimiter=received_.find('\n');if(delimiter==std::string::npos){char bytes[4096];auto count=read(output_,bytes,sizeof bytes);if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return std::nullopt;if(count<=0)throw std::runtime_error("SIMULATOR.BROKER_DISCONNECTED");received_.append(bytes,count);if(received_.size()>8192)throw std::runtime_error("SIMULATOR.BROKER_RESPONSE_TOO_LARGE");delimiter=received_.find('\n');}
 if(delimiter==std::string::npos)return std::nullopt;auto line=received_.substr(0,delimiter);received_.erase(0,delimiter+1);return nlohmann::json::parse(line);
}
}
