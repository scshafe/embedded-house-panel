#include "music_control/music_service.h"
#include <algorithm>

namespace music_control
{
namespace
{
namespace wire = music_control::protocol;

bool same(const wire::State &a, const wire::State &b)
{
	const auto playback = [](const wire::State &s) {
		return s.playback ? std::make_tuple(true, s.playback->is_playing, s.playback->title, s.playback->artists)
				  : std::make_tuple(false, false, std::string(), std::string());
	};
	return a.sign_in == b.sign_in && a.output.state == b.output.state && a.output.name == b.output.name &&
	       a.output.supports_volume == b.output.supports_volume && a.output.volume_percent == b.output.volume_percent &&
	       playback(a) == playback(b) && a.error == b.error;
}

bool printable_name(const std::string &name)
{
	return !name.empty() && name.size() <= 128 &&
	       std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 0x20 && c != 0x7f; });
}
} // namespace

std::string bounded_text(const std::string &text, std::size_t maximum)
{
	std::string clean;
	for (const unsigned char c : text)
		clean += c < 0x20 || c == 0x7f ? ' ' : static_cast<char>(c);
	if (clean.size() <= maximum)
		return clean;
	const std::string ellipsis = "\xE2\x80\xA6";
	std::size_t cut = maximum >= ellipsis.size() ? maximum - ellipsis.size() : 0;
	// Step back over continuation bytes so the cut never splits a character.
	while (cut > 0 && (static_cast<unsigned char>(clean[cut]) & 0xC0) == 0x80)
		--cut;
	return clean.substr(0, cut) + ellipsis;
}

Result<std::unique_ptr<MusicService>> MusicService::open(MusicSettings settings, MusicPorts ports)
{
	using Output = Result<std::unique_ptr<MusicService>>;
	if (!valid_client_id(settings.auth.client_id) || !printable_name(settings.output_name) ||
	    settings.token_directory.empty() || !ports.transport || !ports.steady_now || !ports.random01)
		return Output::failure({"MUSIC.SETTINGS_INVALID", "The client ID, speaker name or ports are invalid", {}, false});
	auto backoff = bhp::durable_store::Backoff::create(settings.backoff, ports.random01);
	if (!backoff.has_value())
		return Output::failure({backoff.error().code, backoff.error().message, {}, false});
	std::unique_ptr<MusicService> service(new MusicService(std::move(settings), std::move(ports), backoff.value()));
	auto restored = service->session_.restore();
	if (!restored.has_value())
		service->error_ = restored.error().code;
	service->due_ = service->ports_.steady_now();
	return Output::success(std::move(service));
}

MusicService::MusicService(MusicSettings settings, MusicPorts ports, bhp::durable_store::Backoff backoff)
	: settings_(std::move(settings)), ports_(std::move(ports)), store_(settings_.token_directory),
	  session_(settings_.auth, ports_.transport, store_, ports_.steady_now),
	  client_(settings_.api, ports_.transport, session_), backoff_(std::move(backoff))
{
}

Result<bool> MusicService::refresh()
{
	auto devices = client_.devices();
	if (!devices.has_value())
		return Result<bool>::failure(devices.error());
	asked_ = true;
	output_.reset();
	for (const auto &device : devices.value())
		if (device.name == settings_.output_name) {
			output_ = device;
			break;
		}
	auto playback = client_.playback();
	if (!playback.has_value())
		return Result<bool>::failure(playback.error());
	playback_ = playback.value();
	return Result<bool>::success(true);
}

std::chrono::milliseconds MusicService::tick()
{
	const auto now = ports_.steady_now();
	// The owner's sign-in puts a new token file in place while this runs
	// (spotify-owner-setup.md, Part 3): read it again, no restart needed. A
	// half-copied file fails to load and is read again when it changes.
	if (store_.changed_on_disk()) {
		auto restored = session_.restore();
		error_ = restored.has_value() ? std::string() : restored.error().code;
		due_ = now;
	}
	if (!session_.signed_in()) {
		output_.reset();
		playback_.reset();
		asked_ = false;
		return kSignedOutCheck;
	}
	if (now < due_)
		return std::chrono::duration_cast<std::chrono::milliseconds>(due_ - now);
	auto refreshed = refresh();
	if (refreshed.has_value()) {
		backoff_.reset();
		error_.clear();
		due_ = now + settings_.refresh_interval;
	} else {
		error_ = refreshed.error().code;
		due_ = now + (refreshed.error().retryable ? backoff_.next_delay()
							  : std::chrono::duration_cast<std::chrono::milliseconds>(
								    settings_.refresh_interval));
	}
	return std::chrono::duration_cast<std::chrono::milliseconds>(due_ - now);
}

Result<bool> MusicService::command(const wire::UiRequest &request, const SpotifyDevice &device)
{
	if (std::holds_alternative<wire::SelectOutputRequest>(request))
		return client_.transfer(device.id, true);
	if (std::holds_alternative<wire::PlayRequest>(request))
		return client_.play(device.id);
	if (std::holds_alternative<wire::PauseRequest>(request))
		return client_.pause(device.id);
	if (std::holds_alternative<wire::NextRequest>(request))
		return client_.next(device.id);
	if (!device.supports_volume)
		return Result<bool>::failure({"MUSIC.VOLUME_UNSUPPORTED", "The speaker takes no volume commands", {}, false});
	return client_.set_volume(device.id, static_cast<int>(std::get<wire::VolumeRequest>(request).percent));
}

std::vector<wire::MusicEvent> MusicService::handle(const wire::UiRequest &request)
{
	if (std::holds_alternative<wire::StatusRequest>(request))
		return {state()};
	const auto name = wire::request_name(request);
	const auto refuse = [&](const std::string &code) {
		return std::vector<wire::MusicEvent>{wire::Refused{name, code}, state()};
	};
	if (!session_.signed_in())
		return refuse("MUSIC.SIGN_IN_REQUIRED");
	if (!output_) {
		auto refreshed = refresh();
		if (!refreshed.has_value()) {
			error_ = refreshed.error().code;
			return refuse(refreshed.error().code);
		}
	}
	if (!output_)
		return refuse("MUSIC.OUTPUT_UNAVAILABLE");
	if (output_->is_restricted)
		return refuse("MUSIC.OUTPUT_RESTRICTED");
	auto done = command(request, *output_);
	due_ = ports_.steady_now() + settings_.settle;
	if (!done.has_value()) {
		// A 404 means the speaker went away since the last device list.
		if (done.error().code == "MUSIC.OUTPUT_UNAVAILABLE")
			output_.reset();
		return refuse(done.error().code);
	}
	error_.clear();
	// Show the command at once; the re-read after `settle` reports what Spotify did.
	if (const auto *volume = std::get_if<wire::VolumeRequest>(&request))
		output_->volume_percent = static_cast<int>(volume->percent);
	else if (std::holds_alternative<wire::SelectOutputRequest>(request) || std::holds_alternative<wire::PlayRequest>(request)) {
		if (!playback_)
			playback_ = SpotifyPlayback{};
		playback_->device = *output_;
		playback_->is_playing = true;
	} else if (std::holds_alternative<wire::PauseRequest>(request) && playback_)
		playback_->is_playing = false;
	return {state()};
}

wire::State MusicService::state()
{
	wire::State next;
	next.sign_in = session_.signed_in() ? "signed_in" : "signed_out";
	next.output.name = settings_.output_name;
	next.error = error_;
	if (session_.signed_in() && asked_) {
		if (!output_)
			next.output.state = "missing";
		else {
			next.output.supports_volume = output_->supports_volume;
			if (output_->volume_percent)
				next.output.volume_percent = static_cast<std::uint32_t>(*output_->volume_percent);
			const bool active = playback_ && playback_->device && playback_->device->id == output_->id;
			next.output.state = output_->is_restricted ? "restricted" : active ? "active" : "available";
			if (active && !output_->is_restricted) {
				std::string artists;
				for (const auto &artist : playback_->artists)
					artists += (artists.empty() ? "" : ", ") + artist;
				next.playback = wire::Playback{playback_->is_playing, bounded_text(playback_->title, 256),
							       bounded_text(artists, 256)};
			}
		}
	}
	next.sequence = last_.sequence;
	if (!same(next, last_) || last_.sequence == 0)
		next.sequence = last_.sequence + 1;
	last_ = next;
	return next;
}
} // namespace music_control
