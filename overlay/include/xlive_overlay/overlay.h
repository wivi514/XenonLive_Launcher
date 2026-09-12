// The XenonLive in-game overlay: friends, invitations and notifications
// drawn over a running title, the way the Steam overlay or the console's
// guide did. Toggled with Shift+Tab (or Back+Start on a pad).
//
// This is a library a port links, not a program. It knows nothing about the
// port's renderer beyond what Render() is handed: a command buffer in the
// middle of the frame and the swapchain image about to be presented, in
// TRANSFER_DST layout. It draws with Dear ImGui through the Vulkan backend
// (dynamic rendering, so no render pass of the port's is involved) and
// hands the image back in the layout it found it.
//
// Threads, because a port has several and ImGui has one:
//   - QueueSdlEvent / SetWindowSize / Toggle / open(): the thread that pumps
//     SDL, i.e. the port's main thread.
//   - OnEvent: any thread; libxlive raises its events on its worker.
//   - Render / Shutdown: the thread that records the frame. ImGui's context
//     lives on this thread and nowhere else; the other calls only queue.
#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

union SDL_Event;

namespace xlive {
class Client;
struct Event;
}  // namespace xlive

namespace xlive_overlay {

struct VulkanHandles {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    // The swapchain's image count and colour format. The backend builds its
    // pipeline against the format, so a swapchain rebuilt with a different
    // one must be reported through `generation` in Render().
    uint32_t image_count = 2;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    uint32_t api_version = VK_API_VERSION_1_3;
};

class Overlay {
public:
    static Overlay& Instance();

    // The game's own client. Everything drawn is read from it — friends(),
    // invites() — and every action is a call on it. title_id is the running
    // title's, for the achievements tab.
    void SetClient(xlive::Client* client, uint32_t title_id);
    // The client's event callback forwards here. Thread-safe.
    void OnEvent(const xlive::Event& event);
    // A notice of the port's own — a co-op call ringing, say — as a toast for
    // `seconds`, shown whether or not the overlay is open and whatever the
    // notifications setting says. A non-empty `tag` replaces any toast with
    // the same tag and lets Dismiss take it down early. Thread-safe.
    void Notify(std::string text, double seconds, std::string tag = {});
    void Dismiss(const std::string& tag);

    // -- main thread ------------------------------------------------------
    // Every SDL event, whether or not the overlay is open; it watches for
    // its own hotkey while closed and takes the input while open. Returns
    // true when the event was the toggle itself.
    bool QueueSdlEvent(const SDL_Event& event);
    // The window's size in points, for mapping mouse positions onto the
    // swapchain; call it whenever it changes.
    void SetWindowSize(int width, int height);
    bool open() const;
    void Toggle();
    // Whether the overlay wants a text input session (an edit box focused).
    bool wants_text_input() const;

    // -- render thread ---------------------------------------------------
    // Draws the overlay (and any toasts, which show while it is closed too)
    // onto `image`, which must be in VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL and
    // is returned in it. `generation` changes whenever the swapchain is
    // rebuilt, so cached image views are dropped. Returns false when nothing
    // was drawn, which is the common frame.
    bool Render(const VulkanHandles& handles, VkCommandBuffer cmd, VkImage image,
                uint32_t width, uint32_t height, uint64_t generation);
    // Before the device goes.
    void Shutdown();

private:
    Overlay();
    ~Overlay();
    struct Impl;
    Impl* impl_;
};

}  // namespace xlive_overlay
