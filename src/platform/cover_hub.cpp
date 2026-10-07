#include <helpers/foobar2000+atl.h>

#include "cover_hub.h"

#include <algorithm>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "../model/cover_accent.h"
#include "image_decoder.h"
#include "logging.h"

namespace bettertabs::cover {

namespace {

constexpr std::uint32_t max_edge = 256;
constexpr std::size_t max_cached = 16;
//! How long a new track may go without delivering art before its accent is dropped. The art
//! loader stays silent for a track without a cover, so silence has to be timed; dropping the
//! old accent at once would flash the fallback colour between two tracks that both have covers.
constexpr double no_art_grace_s = 0.6;

//! FNV-1a over the head of the file plus its length (foo_mediabar's artwork_store).
[[nodiscard]] std::uint64_t content_hash(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const auto head = bytes.first((std::min)(bytes.size(), std::size_t{64 * 1024}));
    for (const std::uint8_t byte : head) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    hash ^= bytes.size();
    hash *= 1099511628211ull;
    return hash;
}

class PlayWatch : public play_callback_impl_base {
public:
    PlayWatch() : play_callback_impl_base(flag_on_playback_new_track | flag_on_playback_stop) {}
    void on_playback_new_track(metadb_handle_ptr) override;
    void on_playback_stop(play_control::t_stop_reason reason) override;
};

//! A named object instead of the add(std::function) helper: the helper allocates a wrapper that
//! remove() never frees, so every subscribe/unsubscribe cycle would leak one.
class ArtWatch : public now_playing_album_art_notify {
public:
    void on_album_art(album_art_data::ptr data) override;
};

struct Cached {
    std::uint64_t hash{0};
    std::optional<fbc::CoverColours> colours;
};

struct State {
    std::vector<Listener*> listeners;
    std::unique_ptr<PlayWatch> play;
    ArtWatch art;
    bool art_registered{false};
    std::optional<fbc::CoverColours> colours;
    std::uint64_t current_hash{0};
    std::uint64_t pending_hash{0};
    //! Bumped per track and on teardown; late timers and decodes compare against it.
    std::uint64_t generation{0};
    bool art_since_track{false};
    std::vector<Cached> cache;
};

State& state() noexcept {
    static State s;
    return s;
}

void publish(const std::optional<fbc::CoverColours>& colours, std::uint64_t hash) noexcept {
    State& s = state();
    s.current_hash = hash;
    if (colours == s.colours) return;
    s.colours = colours;
    // A listener may unsubscribe from inside the callback.
    const std::vector<Listener*> copy = s.listeners;
    for (Listener* listener : copy) {
        if (std::ranges::find(state().listeners, listener) != state().listeners.end()) {
            listener->on_cover_accent_changed();
        }
    }
}

void remember(std::uint64_t hash, const std::optional<fbc::CoverColours>& colours) {
    State& s = state();
    std::erase_if(s.cache, [hash](const Cached& c) { return c.hash == hash; });
    if (s.cache.size() >= max_cached) s.cache.erase(s.cache.begin());
    s.cache.push_back(Cached{hash, colours});
}

void offer(std::span<const std::uint8_t> bytes) {
    State& s = state();
    if (bytes.empty() || s.listeners.empty()) return;
    s.art_since_track = true;
    const std::uint64_t hash = content_hash(bytes);
    if (hash == s.current_hash && s.pending_hash == 0) return;
    if (hash == s.pending_hash) return;
    if (const auto hit = std::ranges::find_if(s.cache, [hash](const Cached& c) { return c.hash == hash; });
        hit != s.cache.end()) {
        s.pending_hash = 0;
        publish(hit->colours, hash);
        return;
    }
    s.pending_hash = hash;
    const std::uint64_t generation = s.generation;
    // Copied once: the host's buffer is only valid for this call.
    auto encoded = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    fb2k::inCpuWorkerThread([hash, generation, encoded] {
        std::optional<fbc::CoverColours> colours;
        try {
            if (const auto image = decode_image(*encoded, max_edge); image) colours = fbc::cover_colours(*image);
        } catch (...) {
        }
        fb2k::inMainThread([hash, generation, colours] {
            State& st = state();
            remember(hash, colours);
            if (st.generation != generation || st.pending_hash != hash) return; // superseded
            st.pending_hash = 0;
            publish(colours, hash);
        });
    });
}

void deliver(album_art_data::ptr data) noexcept {
    if (data.is_empty()) return;
    try {
        offer(std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data->get_ptr()), data->get_size()));
    } catch (...) {
        log::warn("cover accent: could not take the artwork");
    }
}

void peek() noexcept {
    try {
        deliver(now_playing_album_art_notify_manager::get()->current());
    } catch (...) {
    }
}

void ArtWatch::on_album_art(album_art_data::ptr data) { deliver(data); }

void PlayWatch::on_playback_new_track(metadb_handle_ptr) {
    State& s = state();
    const std::uint64_t generation = ++s.generation;
    s.art_since_track = false;
    s.pending_hash = 0;
    peek();
    if (s.art_since_track) return;
    fb2k::callLater(no_art_grace_s, [generation] {
        State& st = state();
        if (st.generation != generation || st.art_since_track || st.listeners.empty()) return;
        publish(std::nullopt, 0);
    });
}

void PlayWatch::on_playback_stop(play_control::t_stop_reason reason) {
    if (reason == play_control::stop_reason_starting_another) return;
    State& s = state();
    ++s.generation;
    s.pending_hash = 0;
    publish(std::nullopt, 0);
}

void start() noexcept {
    State& s = state();
    try {
        s.play = std::make_unique<PlayWatch>();
        now_playing_album_art_notify_manager::get()->add(&s.art);
        s.art_registered = true;
    } catch (const std::exception& e) {
        log::warn(std::string("cover accent unavailable: ") + e.what());
    } catch (...) {
        log::warn("cover accent unavailable");
    }
    s.colours.reset();
    s.current_hash = 0;
    s.pending_hash = 0;
    ++s.generation;
    // The host may already have the cover of the playing track.
    try {
        if (playback_control::get()->is_playing()) peek();
    } catch (...) {
    }
}

void stop() noexcept {
    State& s = state();
    if (s.art_registered) {
        try {
            now_playing_album_art_notify_manager::get()->remove(&s.art);
        } catch (...) {
        }
        s.art_registered = false;
    }
    s.play.reset();
    s.colours.reset();
    s.current_hash = 0;
    s.pending_hash = 0;
    ++s.generation;
    s.cache.clear();
    s.cache.shrink_to_fit();
}

} // namespace

void subscribe(Listener* listener) noexcept {
    if (listener == nullptr) return;
    State& s = state();
    if (std::ranges::find(s.listeners, listener) != s.listeners.end()) return;
    try {
        s.listeners.push_back(listener);
    } catch (...) {
        return;
    }
    if (s.listeners.size() == 1) start();
}

void unsubscribe(Listener* listener) noexcept {
    State& s = state();
    const auto it = std::ranges::find(s.listeners, listener);
    if (it == s.listeners.end()) return;
    s.listeners.erase(it);
    if (s.listeners.empty()) stop();
}

std::optional<std::uint32_t> current() noexcept {
    const auto& colours = state().colours;
    if (!colours) return std::nullopt;
    return colours->primary;
}

std::optional<fbc::CoverColours> current_colours() noexcept { return state().colours; }

void shutdown() noexcept {
    state().listeners.clear();
    stop();
}

} // namespace bettertabs::cover
