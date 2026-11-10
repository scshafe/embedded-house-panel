#pragma once
#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>

namespace bhp::simulator {
/**
 * @canonical ui_service_reconnect -- when the UI tries a lost service socket again.
 *
 * After a loss or a failed attempt the next try waits `first` (1 s), then twice
 * as long each time, up to `maximum` (60 s). A connection that works resets it.
 * The clock is injected so tests step it instead of sleeping.
 */
class ReconnectSchedule final {
public:
 using Clock=std::function<std::chrono::steady_clock::time_point()>;
 explicit ReconnectSchedule(Clock now=[]{return std::chrono::steady_clock::now();},
                            std::chrono::milliseconds first=std::chrono::seconds(1),
                            std::chrono::milliseconds maximum=std::chrono::seconds(60))
  :now_(std::move(now)),first_(first),maximum_(maximum),delay_(first){}
 void set_clock(Clock now){now_=std::move(now);}
 /** True when a try is allowed now. */
 bool due() const {return now_()>=next_;}
 /** A loss or a failed try: the next one waits the current delay, which then doubles. */
 void failed(){next_=now_()+delay_;delay_=std::min(maximum_,delay_*2);}
 /** A working connection: the next loss waits `first` again. */
 void succeeded(){delay_=first_;next_={};}
 /** How long the next failure will wait (for status and tests). */
 std::chrono::milliseconds next_delay() const {return delay_;}
private:
 Clock now_;
 std::chrono::milliseconds first_,maximum_,delay_;
 std::chrono::steady_clock::time_point next_{};
};
}
