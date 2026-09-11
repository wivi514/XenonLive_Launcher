// Achievement tiles and title tiles, from the server, as SDL textures.
//
// The server serves a title's art publicly with cache headers, and the art
// never changes (it is the SPA's own PNGs), so each image is fetched once
// onto disk — launcher/images/<title>/<name>.png — and decoded from there
// on every later start. Fetches run on one background thread; the UI asks
// Get() every frame and draws whatever has arrived.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

struct SDL_Renderer;
struct SDL_Texture;

namespace launcher {

struct Image {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
};

class ImageCache {
public:
    ImageCache() = default;
    ~ImageCache();
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    // dir is the launcher's own directory; images/ goes under it.
    void Open(SDL_Renderer* renderer, const std::filesystem::path& dir);
    // The server root the URLs below are relative to. Changing it drops
    // nothing on disk: a title's art is the same on every server that
    // imported it.
    void set_server(const std::string& url) { server_ = url; }

    // The tile for one achievement, or a null texture while it is not here
    // yet (a fetch is queued the first time it is asked for). A 404 is
    // remembered for the session so a title without art is asked once.
    Image Achievement(uint32_t title_id, uint16_t achievement_id);
    // The title's own tile.
    Image Title(uint32_t title_id);

private:
    Image Get(const std::string& key, const std::string& path, const std::string& file);
    bool Decode(const std::string& key, const std::filesystem::path& file, Image& out);
    void Run();

    SDL_Renderer* renderer_ = nullptr;
    std::filesystem::path dir_;
    std::string server_;
    std::map<std::string, Image> loaded_;
    std::set<std::string> missing_;   // asked, not on the server
    std::set<std::string> in_flight_; // asked, fetch queued or running

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> running_{false};
    struct Job {
        std::string key;
        std::string url;
        std::filesystem::path file;
    };
    std::deque<Job> queue_;
    // Finished fetches: key -> ok. Drained by the UI thread in Get().
    std::map<std::string, bool> finished_;
};

}  // namespace launcher
