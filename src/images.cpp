#include "images.h"

#include <SDL.h>
#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include "paths.h"

namespace launcher {

namespace {

size_t WriteToFile(char* data, size_t size, size_t count, void* user) {
    return std::fwrite(data, 1, size * count, static_cast<std::FILE*>(user));
}

std::string HexU32(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", v);
    return buf;
}

// One image to a .part file, then renamed: a reader sees a whole PNG or
// none. False for anything but a 200.
bool Fetch(const std::string& url, const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::filesystem::path part = file.string() + ".part";
    std::FILE* out = std::fopen(part.string().c_str(), "wb");
    if (!out) return false;
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(out);
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "XenonLive-Launcher/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    // The same trust settings the library uses for the same server.
    if (xlive::Env("XLIVE_ALLOW_INSECURE") == "1") {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (const std::string ca = xlive::Env("XLIVE_CA_FILE"); !ca.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca.c_str());
    }
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    std::fclose(out);
    if (code != CURLE_OK || status != 200) {
        std::filesystem::remove(part, ec);
        return false;
    }
    std::filesystem::rename(part, file, ec);
    return !ec;
}

}  // namespace

ImageCache::~ImageCache() {
    running_.store(false);
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    for (auto& [key, image] : loaded_) {
        if (image.texture) SDL_DestroyTexture(image.texture);
    }
}

void ImageCache::Open(SDL_Renderer* renderer, const std::filesystem::path& dir) {
    renderer_ = renderer;
    dir_ = dir / "images";
    running_.store(true);
    thread_ = std::thread([this] { Run(); });
}

Image ImageCache::Achievement(uint32_t title_id, uint16_t achievement_id) {
    const std::string title = HexU32(title_id);
    const std::string name = "ach-" + std::to_string(achievement_id);
    return Get(title + "/" + name,
               "/v1/titles/" + title + "/achievements/" + std::to_string(achievement_id) + "/image",
               title + "/" + name + ".png");
}

Image ImageCache::Title(uint32_t title_id) {
    const std::string title = HexU32(title_id);
    return Get(title + "/tile", "/v1/titles/" + title + "/image", title + "/tile.png");
}

Image ImageCache::Get(const std::string& key, const std::string& path, const std::string& file) {
    if (!renderer_) return {};
    if (auto found = loaded_.find(key); found != loaded_.end()) return found->second;
    if (missing_.count(key)) return {};

    // Anything the worker finished since the last frame.
    std::map<std::string, bool> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished.swap(finished_);
    }
    for (const auto& [done_key, ok] : finished) {
        in_flight_.erase(done_key);
        if (!ok) missing_.insert(done_key);
    }
    if (missing_.count(key)) return {};

    const std::filesystem::path on_disk = dir_ / file;
    std::error_code ec;
    if (std::filesystem::is_regular_file(on_disk, ec)) {
        Image image;
        if (Decode(key, on_disk, image)) {
            loaded_[key] = image;
            return image;
        }
        // A file that does not decode is not one worth keeping.
        std::filesystem::remove(on_disk, ec);
        missing_.insert(key);
        return {};
    }

    if (!in_flight_.count(key) && !server_.empty()) {
        in_flight_.insert(key);
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({key, server_ + path, on_disk});
        wake_.notify_all();
    }
    return {};
}

Image ImageCache::Local(const std::filesystem::path& file) {
    if (!renderer_) return {};
    const std::string key = "local:" + file.string();
    if (auto found = loaded_.find(key); found != loaded_.end()) return found->second;
    if (missing_.count(key)) return {};
    Image image;
    if (Decode(key, file, image)) {
        loaded_[key] = image;
        return image;
    }
    missing_.insert(key);
    return {};
}

bool ImageCache::Decode(const std::string& key, const std::filesystem::path& file, Image& out) {
    std::FILE* in = std::fopen(file.string().c_str(), "rb");
    if (!in) return false;
    std::string bytes;
    char buffer[1 << 14];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0) bytes.append(buffer, n);
    std::fclose(in);

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                            int(bytes.size()), &width, &height, &channels, 4);
    if (!pixels) {
        std::fprintf(stderr, "[images] %s: %s\n", key.c_str(), stbi_failure_reason());
        return false;
    }
    SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture) {
        SDL_UpdateTexture(texture, nullptr, pixels, width * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    stbi_image_free(pixels);
    if (!texture) {
        std::fprintf(stderr, "[images] %s: SDL_CreateTexture: %s\n", key.c_str(), SDL_GetError());
        return false;
    }
    out.texture = texture;
    out.width = width;
    out.height = height;
    return true;
}

void ImageCache::Run() {
    while (running_.load()) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return !running_.load() || !queue_.empty(); });
            if (!running_.load()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        const bool ok = Fetch(job.url, job.file);
        std::lock_guard<std::mutex> lock(mutex_);
        finished_[job.key] = ok;
    }
}

}  // namespace launcher
