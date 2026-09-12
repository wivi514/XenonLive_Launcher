// Which changes in the friends list are worth a notification.
//
// "Frank is playing Case Zero" should be said once, when Frank starts
// playing — not again because our own gateway blinked and came back with
// a fresh snapshot, or because Frank's game re-sent its presence when its
// window regained focus. Both look, to a naive diff of the list, like
// Frank going away and coming back. So this remembers what was last SAID
// about each friend and says something new only when it is new: a game
// they were not in, or a return after being gone for real (longer than the
// grace a reconnect takes).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "xlive/client.h"

namespace xlive::theme {

class PresenceAnnouncer {
public:
    struct Line {
        std::string gamertag;
        std::string text;  // "is playing Dead Rising 2: Case Zero", "is online"
    };

    // A friend must be offline this long before coming back counts as news.
    static constexpr double kGraceSeconds = 60.0;

    // Feeds the current list; returns what to announce. The first call is
    // the baseline and announces nothing.
    std::vector<Line> Update(const std::vector<xlive::Client::Friend>& now, double now_seconds) {
        std::vector<Line> out;
        for (const auto& f : now) {
            if (!f.is_friend()) continue;
            Seen& seen = seen_[f.xuid];
            const bool online = f.presence.online();
            const bool playing = f.presence.state == xlive::Client::PresenceState::Playing;
            const uint32_t title = playing ? f.presence.title_id : 0;

            if (!seen.known) {
                seen.known = true;
                seen.said_online = online;
                seen.said_title = title;
                seen.offline_since = online ? -1.0 : now_seconds;
                continue;
            }
            if (!online) {
                if (seen.offline_since < 0.0) seen.offline_since = now_seconds;
                continue;
            }
            // Online now. Gone long enough to be back for real?
            const bool was_gone = seen.offline_since >= 0.0 &&
                                  now_seconds - seen.offline_since >= kGraceSeconds;
            seen.offline_since = -1.0;
            if (title == 0) {
                // Online without a title. Their game's connection blinking
                // while their launcher stays looks the same as quitting to
                // the launcher; only after the grace does it count as having
                // left, so the same game resumed is not "is playing" again.
                if (seen.said_title != 0) {
                    if (seen.left_since < 0.0) seen.left_since = now_seconds;
                    if (now_seconds - seen.left_since >= kGraceSeconds) {
                        seen.said_title = 0;
                        seen.left_since = -1.0;
                    }
                }
                if (was_gone || !seen.said_online) {
                    out.push_back({f.gamertag, "is online"});
                    seen.said_online = true;
                    seen.said_title = 0;
                    seen.left_since = -1.0;
                }
                continue;
            }
            seen.left_since = -1.0;
            const bool new_game = title != seen.said_title;
            const bool came_back = was_gone || !seen.said_online;
            if (new_game || came_back) {
                out.push_back({f.gamertag, "is playing " + f.presence.title_name});
                seen.said_online = true;
                seen.said_title = title;
            }
        }
        // A friend that dropped off the list is forgotten.
        for (auto it = seen_.begin(); it != seen_.end();) {
            bool present = false;
            for (const auto& f : now) present |= f.xuid == it->first;
            it = present ? std::next(it) : seen_.erase(it);
        }
        return out;
    }

    void Reset() { seen_.clear(); }

private:
    struct Seen {
        bool known = false;
        bool said_online = false;   // the last thing said had them online
        uint32_t said_title = 0;    // ...and in this title (0: none)
        double offline_since = -1.0;
        double left_since = -1.0;   // online without the title since
    };
    std::map<uint64_t, Seen> seen_;
};

}  // namespace xlive::theme
