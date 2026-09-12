#include "xlive_overlay/overlay.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <xlive/client.h>

#include "imgui.h"
#include "theme.h"
#include "imgui_impl_vulkan.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "stb_image.h"

// The SDL2 backend's key map, which is the one table nobody should write
// twice. Its NewFrame is not used — it calls into SDL from whatever thread
// runs it, and this frame is built on the render thread — so events are
// translated here and fed to ImGui's io directly.
ImGuiKey ImGui_ImplSDL2_KeyEventToImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);

namespace xlive_overlay {

namespace {

using Client = xlive::Client;

// The launcher's palette, so the overlay looks like the thing that started
// the game.
const ImVec4& kGreen = xlive::theme::kLime;
const ImVec4& kAmber = xlive::theme::kAmber;
const ImVec4& kDim = xlive::theme::kMuted;

struct Toast {
    std::string text;
    double expires_at = 0;
};

struct PendingSocial {
    Client::Ticket ticket = 0;
    std::string what;
};

std::string PresenceLine(const Client::Presence& p) {
    switch (p.state) {
        case Client::PresenceState::Offline: return "offline";
        case Client::PresenceState::Online:  return "online";
        case Client::PresenceState::Playing: {
            std::string line = "playing " + (p.title_name.empty() ? "a game" : p.title_name);
            if (!p.rich_text.empty() && p.rich_text != p.title_name) line += " - " + p.rich_text;
            if (p.joinable) line += " (joinable)";
            return line;
        }
    }
    return "";
}

ImGuiKey PadButtonKey(uint8_t button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A:             return ImGuiKey_GamepadFaceDown;
        case SDL_CONTROLLER_BUTTON_B:             return ImGuiKey_GamepadFaceRight;
        case SDL_CONTROLLER_BUTTON_X:             return ImGuiKey_GamepadFaceLeft;
        case SDL_CONTROLLER_BUTTON_Y:             return ImGuiKey_GamepadFaceUp;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:       return ImGuiKey_GamepadDpadUp;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return ImGuiKey_GamepadDpadDown;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return ImGuiKey_GamepadDpadLeft;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return ImGuiKey_GamepadDpadRight;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return ImGuiKey_GamepadL1;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return ImGuiKey_GamepadR1;
        case SDL_CONTROLLER_BUTTON_START:         return ImGuiKey_GamepadStart;
        case SDL_CONTROLLER_BUTTON_BACK:          return ImGuiKey_GamepadBack;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return ImGuiKey_GamepadL3;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return ImGuiKey_GamepadR3;
        default: return ImGuiKey_None;
    }
}

}  // namespace

// A tile uploaded to the GPU: its image, memory, view, and the descriptor
// ImGui draws it with.
struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    int width = 0, height = 0;
};

struct Overlay::Impl {
    // -- shared ---------------------------------------------------------------
    std::atomic<Client*> client{nullptr};
    std::atomic<uint32_t> title_id{0};
    std::atomic<bool> open{false};
    std::atomic<bool> wants_text{false};
    std::atomic<int> window_w{0}, window_h{0};

    std::mutex events_mutex;
    std::deque<xlive::Event> events;
    std::mutex input_mutex;
    std::vector<SDL_Event> input;

    // The pad chord, tracked on the main thread.
    bool pad_back = false, pad_start = false, chord_fired = false;

    // -- render thread only ---------------------------------------------------
    bool initialized = false;
    VulkanHandles handles;
    uint64_t generation = 0;
    std::map<VkImage, VkImageView> views;
    std::chrono::steady_clock::time_point last_frame{};
    float scale = 1.0f;
    xlive::theme::Fonts fonts;

    // UI state.
    std::vector<Toast> toasts;
    std::vector<PendingSocial> pending;
    Client::Ticket presence_ticket = 0;
    Client::Presence mine;
    bool mine_loaded = false;
    Client::Ticket refresh_ticket = 0;
    bool was_open = false;
    std::vector<Client::Friend> last_friends;
    bool friends_baseline = false;

    // The achievements tab: the title's definitions and this player's
    // state, read once per opening (and again after an unlock), and the
    // tiles, fetched once per run and kept on the GPU.
    Client::Ticket title_ticket = 0;
    Client::TitleResult title;
    bool title_loaded = false;
    std::string title_error;
    bool title_stale = true;
    std::map<uint32_t, Client::Ticket> image_tickets;  // image id -> ticket
    std::map<uint32_t, Texture> tiles;                 // image id -> texture
    std::map<uint32_t, bool> tiles_missing;            // image id -> asked, none
    VkCommandPool upload_pool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    bool UploadTile(uint32_t image_id, const std::string& bytes);
    void DestroyTextures();
    void DrawAchievements(Client& c, float width);

    // Our own clock for the toasts, not ImGui's: a toast can arrive on a
    // frame where ImGui is never touched.
    static double Now() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void Push(std::string text, double seconds = 5.0) {
        toasts.push_back({std::move(text), Now() + seconds});
        while (toasts.size() > 5) toasts.erase(toasts.begin());
    }
    void Issue(Client::Ticket ticket, std::string what) {
        if (ticket) pending.push_back({ticket, std::move(what)});
    }

    void Init(const VulkanHandles& h);
    void DropViews();
    VkImageView ViewFor(VkImage image);
    void DrainEvents(Client& c);
    void DrainInput(uint32_t width, uint32_t height);
    void PollTickets(Client& c);
    void DrawPanel(Client& c, uint32_t width, uint32_t height);
    void DrawToasts(uint32_t width, uint32_t height);
};

Overlay& Overlay::Instance() {
    static Overlay instance;
    return instance;
}

Overlay::Overlay() : impl_(new Impl) {}
Overlay::~Overlay() { delete impl_; }

void Overlay::SetClient(xlive::Client* client, uint32_t title_id) {
    impl_->client.store(client);
    impl_->title_id.store(title_id);
}

void Overlay::OnEvent(const xlive::Event& event) {
    std::lock_guard<std::mutex> lock(impl_->events_mutex);
    impl_->events.push_back(event);
    while (impl_->events.size() > 64) impl_->events.pop_front();
}

bool Overlay::open() const { return impl_->open.load(); }
bool Overlay::wants_text_input() const { return impl_->wants_text.load(); }
void Overlay::Toggle() { impl_->open.store(!impl_->open.load()); }

void Overlay::SetWindowSize(int width, int height) {
    impl_->window_w.store(width);
    impl_->window_h.store(height);
}

bool Overlay::QueueSdlEvent(const SDL_Event& event) {
    // The hotkeys, seen open or closed.
    if (event.type == SDL_KEYDOWN && !event.key.repeat &&
        event.key.keysym.scancode == SDL_SCANCODE_TAB && (event.key.keysym.mod & KMOD_SHIFT)) {
        Toggle();
        return true;
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
        const bool down = event.type == SDL_CONTROLLERBUTTONDOWN;
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) impl_->pad_back = down;
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START) impl_->pad_start = down;
        if (impl_->pad_back && impl_->pad_start && !impl_->chord_fired) {
            impl_->chord_fired = true;
            Toggle();
            return true;
        }
        if (!impl_->pad_back && !impl_->pad_start) impl_->chord_fired = false;
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE && down) {
            Toggle();
            return true;
        }
    }
    if (!impl_->open.load()) return false;
    switch (event.type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
        case SDL_WINDOWEVENT: {
            std::lock_guard<std::mutex> lock(impl_->input_mutex);
            impl_->input.push_back(event);
            while (impl_->input.size() > 512) impl_->input.erase(impl_->input.begin());
            break;
        }
        default:
            break;
    }
    return false;
}

// -- Vulkan ----------------------------------------------------------------

void Overlay::Impl::Init(const VulkanHandles& h) {
    handles = h;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    // The cursor is the game's, and SDL cursor calls belong to the main
    // thread anyway.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "xlive_overlay";

    // The launcher's theme and font, translucent enough to keep the game in
    // view. The font is set for 720p and FontGlobalScale takes it up from
    // there; the backend uploads the atlas on the first
    // ImGui_ImplVulkan_NewFrame, which is the first frame with something to
    // draw — a queue submit and a wait, once, when the first toast appears.
    xlive::theme::Apply(ImGui::GetStyle(), 1.0f, 0.94f);
    fonts = xlive::theme::LoadFonts(io, 20.0f);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = h.api_version;
    info.Instance = h.instance;
    info.PhysicalDevice = h.physical;
    info.Device = h.device;
    info.QueueFamily = h.queue_family;
    info.Queue = h.queue;
    // One descriptor per texture: the font atlas and every achievement tile
    // (a title has a dozen; a second title's tiles never load in the same
    // process). Too small a pool fails the allocation and the driver then
    // faults on the null set — the first version had 8.
    info.DescriptorPoolSize = 128;
    info.MinImageCount = std::max(2u, h.image_count);
    info.ImageCount = std::max(2u, h.image_count);
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    static VkFormat format;
    format = h.color_format;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
    if (!ImGui_ImplVulkan_Init(&info)) {
        std::fprintf(stderr, "[overlay] ImGui_ImplVulkan_Init failed; overlay disabled\n");
        ImGui::DestroyContext();
        return;
    }
    initialized = true;
    last_frame = std::chrono::steady_clock::now();
}

void Overlay::Impl::DropViews() {
    for (auto& [image, view] : views) vkDestroyImageView(handles.device, view, nullptr);
    views.clear();
}

VkImageView Overlay::Impl::ViewFor(VkImage image) {
    if (auto found = views.find(image); found != views.end()) return found->second;
    VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ci.image = image;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format = handles.color_format;
    ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(handles.device, &ci, nullptr, &view) != VK_SUCCESS) return VK_NULL_HANDLE;
    views[image] = view;
    return view;
}

void Overlay::Shutdown() {
    Impl& s = *impl_;
    if (!s.initialized) return;
    vkDeviceWaitIdle(s.handles.device);
    s.DropViews();
    s.DestroyTextures();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    s.initialized = false;
}

// -- tiles on the GPU -------------------------------------------------------------
//
// A synchronous upload on the render thread: a staging buffer, a one-shot
// command buffer, a submit and a wait. The port's own texture uploads do
// the same, and so does ImGui's font atlas; a dozen 64x64 tiles when the
// tab first opens is not a stall anyone can see.

namespace {

uint32_t FindMemoryType(VkPhysicalDevice physical, uint32_t type_bits, VkMemoryPropertyFlags wanted) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
    }
    return UINT32_MAX;
}

}  // namespace

bool Overlay::Impl::UploadTile(uint32_t image_id, const std::string& bytes) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                            int(bytes.size()), &w, &h, &channels, 4);
    if (!pixels) return false;
    const VkDevice device = handles.device;
    const VkDeviceSize size = VkDeviceSize(w) * VkDeviceSize(h) * 4;
    Texture t;
    t.width = w;
    t.height = h;
    bool ok = false;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    do {
        if (upload_pool == VK_NULL_HANDLE) {
            VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pi.queueFamilyIndex = handles.queue_family;
            if (vkCreateCommandPool(device, &pi, nullptr, &upload_pool) != VK_SUCCESS) break;
        }
        if (sampler == VK_NULL_HANDLE) {
            VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            si.magFilter = si.minFilter = VK_FILTER_LINEAR;
            si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            si.maxLod = 1.0f;
            if (vkCreateSampler(device, &si, nullptr, &sampler) != VK_SUCCESS) break;
        }
        // The image.
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent = {uint32_t(w), uint32_t(h), 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &ii, nullptr, &t.image) != VK_SUCCESS) break;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, t.image, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(handles.physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device, &ai, nullptr, &t.memory) != VK_SUCCESS) break;
        if (vkBindImageMemory(device, t.image, t.memory, 0) != VK_SUCCESS) break;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &vi, nullptr, &t.view) != VK_SUCCESS) break;
        // The staging buffer, filled from the CPU.
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (vkCreateBuffer(device, &bi, nullptr, &staging) != VK_SUCCESS) break;
        VkMemoryRequirements breq;
        vkGetBufferMemoryRequirements(device, staging, &breq);
        VkMemoryAllocateInfo bai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        bai.allocationSize = breq.size;
        bai.memoryTypeIndex = FindMemoryType(handles.physical, breq.memoryTypeBits,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (bai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(device, &bai, nullptr, &staging_memory) != VK_SUCCESS) break;
        if (vkBindBufferMemory(device, staging, staging_memory, 0) != VK_SUCCESS) break;
        void* mapped = nullptr;
        if (vkMapMemory(device, staging_memory, 0, size, 0, &mapped) != VK_SUCCESS) break;
        std::memcpy(mapped, pixels, size_t(size));
        vkUnmapMemory(device, staging_memory);
        // One command buffer: undefined -> transfer dst, copy, -> shader read.
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool = upload_pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &cai, &cmd) != VK_SUCCESS) break;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
        VkImageMemoryBarrier to_dst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_dst.srcQueueFamilyIndex = to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_dst.image = t.image;
        to_dst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_dst);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {uint32_t(w), uint32_t(h), 1};
        vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        VkImageMemoryBarrier to_read = to_dst;
        to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_read);
        vkEndCommandBuffer(cmd);
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (vkCreateFence(device, &fi, nullptr, &fence) != VK_SUCCESS) break;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        if (vkQueueSubmit(handles.queue, 1, &submit, fence) != VK_SUCCESS) break;
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        t.descriptor = ImGui_ImplVulkan_AddTexture(sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        ok = t.descriptor != VK_NULL_HANDLE;
    } while (false);
    stbi_image_free(pixels);
    if (fence) vkDestroyFence(device, fence, nullptr);
    if (cmd) vkFreeCommandBuffers(device, upload_pool, 1, &cmd);
    if (staging) vkDestroyBuffer(device, staging, nullptr);
    if (staging_memory) vkFreeMemory(device, staging_memory, nullptr);
    if (!ok) {
        if (t.view) vkDestroyImageView(device, t.view, nullptr);
        if (t.image) vkDestroyImage(device, t.image, nullptr);
        if (t.memory) vkFreeMemory(device, t.memory, nullptr);
        return false;
    }
    tiles[image_id] = t;
    return true;
}

void Overlay::Impl::DestroyTextures() {
    const VkDevice device = handles.device;
    for (auto& [id, t] : tiles) {
        if (t.descriptor) ImGui_ImplVulkan_RemoveTexture(t.descriptor);
        if (t.view) vkDestroyImageView(device, t.view, nullptr);
        if (t.image) vkDestroyImage(device, t.image, nullptr);
        if (t.memory) vkFreeMemory(device, t.memory, nullptr);
    }
    tiles.clear();
    if (sampler) vkDestroySampler(device, sampler, nullptr);
    sampler = VK_NULL_HANDLE;
    if (upload_pool) vkDestroyCommandPool(device, upload_pool, nullptr);
    upload_pool = VK_NULL_HANDLE;
}

// -- input and events ------------------------------------------------------

void Overlay::Impl::DrainInput(uint32_t width, uint32_t height) {
    std::vector<SDL_Event> batch;
    {
        std::lock_guard<std::mutex> lock(input_mutex);
        batch.swap(input);
    }
    ImGuiIO& io = ImGui::GetIO();
    // Mouse positions arrive in window points; the swapchain is in pixels.
    const int ww = window_w.load(), wh = window_h.load();
    const float sx = ww > 0 ? float(width) / float(ww) : 1.0f;
    const float sy = wh > 0 ? float(height) / float(wh) : 1.0f;
    for (const SDL_Event& e : batch) {
        switch (e.type) {
            case SDL_MOUSEMOTION:
                io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                io.AddMousePosEvent(float(e.motion.x) * sx, float(e.motion.y) * sy);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                int button = -1;
                if (e.button.button == SDL_BUTTON_LEFT) button = 0;
                if (e.button.button == SDL_BUTTON_RIGHT) button = 1;
                if (e.button.button == SDL_BUTTON_MIDDLE) button = 2;
                if (button >= 0) {
                    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                    io.AddMouseButtonEvent(button, e.type == SDL_MOUSEBUTTONDOWN);
                }
                break;
            }
            case SDL_MOUSEWHEEL:
                io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                io.AddMouseWheelEvent(float(e.wheel.x), float(e.wheel.y));
                break;
            case SDL_TEXTINPUT:
                io.AddInputCharactersUTF8(e.text.text);
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                const SDL_Keymod mod = SDL_Keymod(e.key.keysym.mod);
                io.AddKeyEvent(ImGuiMod_Ctrl, (mod & KMOD_CTRL) != 0);
                io.AddKeyEvent(ImGuiMod_Shift, (mod & KMOD_SHIFT) != 0);
                io.AddKeyEvent(ImGuiMod_Alt, (mod & KMOD_ALT) != 0);
                io.AddKeyEvent(ImGuiMod_Super, (mod & KMOD_GUI) != 0);
                const ImGuiKey key =
                    ImGui_ImplSDL2_KeyEventToImGuiKey(e.key.keysym.sym, e.key.keysym.scancode);
                if (key != ImGuiKey_None) io.AddKeyEvent(key, e.type == SDL_KEYDOWN);
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                const ImGuiKey key = PadButtonKey(e.cbutton.button);
                if (key != ImGuiKey_None) io.AddKeyEvent(key, e.type == SDL_CONTROLLERBUTTONDOWN);
                break;
            }
            case SDL_CONTROLLERAXISMOTION: {
                const float v = float(e.caxis.value) / 32767.0f;
                const float dead = 0.25f;
                const auto analog = [&](ImGuiKey negative, ImGuiKey positive) {
                    io.AddKeyAnalogEvent(negative, v < -dead, v < -dead ? -v : 0.0f);
                    io.AddKeyAnalogEvent(positive, v > dead, v > dead ? v : 0.0f);
                };
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                    analog(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight);
                } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    analog(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown);
                }
                break;
            }
            default:
                break;
        }
    }
}

void Overlay::Impl::DrainEvents(Client& c) {
    std::deque<xlive::Event> batch;
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        batch.swap(events);
    }
    for (const xlive::Event& event : batch) {
        switch (event.kind) {
            case xlive::EventKind::AchievementUnlocked:
                title_stale = true;
                Push("Achievement unlocked: " +
                         (event.achievement_name.empty() ? "#" + std::to_string(event.achievement_id)
                                                         : event.achievement_name) +
                         (event.score ? " (" + std::to_string(event.score) + " G)" : ""),
                     7.0);
                break;
            case xlive::EventKind::InviteReceived:
                Push(event.gamertag + " invited you to play - Shift+Tab to answer", 12.0);
                break;
            case xlive::EventKind::InviteAccepted:
                Push("Joining " + event.gamertag + "'s game", 6.0);
                break;
            case xlive::EventKind::InviteAnswered:
                Push(event.gamertag + (event.accepted ? " accepted" : " declined") +
                     " your invitation");
                break;
            case xlive::EventKind::SigninChanged:
                Push(c.identity().online() ? "Signed in as " + c.identity().gamertag
                                           : "Signed out of XenonLive");
                break;
            case xlive::EventKind::ConnectionChanged:
                if (!c.online()) Push("XenonLive connection lost; retrying");
                break;
            case xlive::EventKind::FriendsChanged: {
                const auto now = c.friends();
                if (friends_baseline) {
                    for (const auto& f : now) {
                        const Client::Friend* was = nullptr;
                        for (const auto& old : last_friends) {
                            if (old.xuid == f.xuid) was = &old;
                        }
                        if (f.presence.online() && (!was || !was->presence.online())) {
                            Push(f.gamertag + (f.presence.state == Client::PresenceState::Playing
                                                   ? " is playing " + f.presence.title_name
                                                   : " is online"));
                        }
                        if (f.relation == Client::Relation::RequestReceived &&
                            (!was || was->relation != Client::Relation::RequestReceived)) {
                            Push(f.gamertag + " wants to be your friend");
                        }
                    }
                }
                last_friends = now;
                friends_baseline = true;
                break;
            }
        }
    }
}

void Overlay::Impl::PollTickets(Client& c) {
    for (size_t i = 0; i < pending.size();) {
        Client::SocialResult result;
        const auto status = c.Poll(pending[i].ticket, result);
        if (status == Client::OpStatus::Pending) {
            ++i;
            continue;
        }
        if (status == Client::OpStatus::Failed) Push(pending[i].what + " failed: " + result.error);
        pending.erase(pending.begin() + long(i));
    }
    if (presence_ticket) {
        Client::SocialResult result;
        const auto status = c.Poll(presence_ticket, result);
        if (status != Client::OpStatus::Pending) {
            presence_ticket = 0;
            mine_loaded = status == Client::OpStatus::Succeeded;
            mine = mine_loaded ? result.presence : Client::Presence{};
        }
    }
    if (refresh_ticket) {
        Client::SocialResult result;
        if (c.Poll(refresh_ticket, result) != Client::OpStatus::Pending) refresh_ticket = 0;
    }
    if (title_ticket) {
        Client::TitleResult result;
        const auto status = c.Poll(title_ticket, result);
        if (status != Client::OpStatus::Pending) {
            title_ticket = 0;
            if (status == Client::OpStatus::Succeeded) {
                title = std::move(result);
                title_loaded = true;
                title_error.clear();
                // The tiles, once per run: the server holds them for the
                // ids the definitions name, and they never change.
                for (const auto& a : title.title.achievements) {
                    if (a.image_id == 0 || tiles.count(a.image_id) || tiles_missing.count(a.image_id) ||
                        image_tickets.count(a.image_id)) {
                        continue;
                    }
                    if (!title.title.images.empty() && !title.title.HasImage(a.image_id)) {
                        tiles_missing[a.image_id] = true;
                        continue;
                    }
                    image_tickets[a.image_id] = c.LoadImage(title.title.title_id, a.image_id);
                }
            } else {
                title_error = result.error.empty() ? "unknown" : result.error;
            }
        }
    }
    for (auto it = image_tickets.begin(); it != image_tickets.end();) {
        Client::ImageResult result;
        const auto status = c.Poll(it->second, result);
        if (status == Client::OpStatus::Pending) {
            ++it;
            continue;
        }
        if (status != Client::OpStatus::Succeeded || !UploadTile(it->first, result.bytes)) {
            tiles_missing[it->first] = true;
        }
        it = image_tickets.erase(it);
    }
}

// -- drawing ------------------------------------------------------------------

void Overlay::Impl::DrawToasts(uint32_t width, uint32_t height) {
    const double now = Now();
    std::erase_if(toasts, [&](const Toast& t) { return t.expires_at <= now; });
    if (toasts.empty()) return;
    const float pad = 16.0f * scale;
    float y = float(height) - pad;
    for (size_t i = toasts.size(); i-- > 0;) {
        ImGui::SetNextWindowBgAlpha(0.9f);
        ImGui::SetNextWindowPos(ImVec2(float(width) - pad, y), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f * scale, 0), ImVec2(460.0f * scale, FLT_MAX));
        ImGui::PushID(int(i));
        if (ImGui::Begin("##toast", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoInputs)) {
            const ImVec2 min = ImGui::GetWindowPos();
            const ImVec2 max(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
            ImGui::GetWindowDrawList()->AddRectFilled(
                min, ImVec2(min.x + 5.0f * scale, max.y), ImGui::GetColorU32(xlive::theme::kLime),
                ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersLeft);
            ImGui::PushTextWrapPos(440.0f * scale);
            ImGui::TextUnformatted(toasts[i].text.c_str());
            ImGui::PopTextWrapPos();
            y -= ImGui::GetWindowSize().y + 8.0f * scale;
        }
        ImGui::End();
        ImGui::PopID();
    }
}

void Overlay::Impl::DrawPanel(Client& c, uint32_t width, uint32_t height) {
    // Dim the game.
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(float(width), float(height)),
                                                  IM_COL32(0, 0, 0, 150));

    const ImVec2 size(std::min(float(width) * 0.7f, 860.0f * scale),
                      std::min(float(height) * 0.8f, 620.0f * scale));
    ImGui::SetNextWindowPos(ImVec2(float(width) * 0.5f, float(height) * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin("XenonLive", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);

    const xlive::Identity id = c.identity();
    ImGui::PushFont(fonts.title);
    ImGui::TextColored(xlive::theme::kLime, "Xenon");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted("Live");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 16.0f * scale);
    ImGui::PushFont(fonts.heading);
    ImGui::Text("%s", id.gamertag.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(c.online() ? kGreen : kAmber, c.online() ? "online" : "offline");
    const char* hint = "Shift+Tab or Back+Start closes";
    ImGui::SameLine(size.x - ImGui::CalcTextSize(hint).x - ImGui::GetStyle().WindowPadding.x);
    ImGui::TextDisabled("%s", hint);
    ImGui::Separator();

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Friends")) {
            const bool can_invite = mine_loaded && mine.session_id != 0;
            if (presence_ticket) {
                ImGui::TextDisabled("reading your session...");
            } else if (can_invite) {
                ImGui::TextDisabled("in a session: friends can be invited");
            } else {
                ImGui::TextDisabled("not in a joinable session yet");
            }
            ImGui::SameLine(size.x - 110.0f * scale);
            if (ImGui::SmallButton("Refresh")) {
                Issue(c.RefreshFriends(), "Refresh");
                if (!presence_ticket) presence_ticket = c.LoadMyPresence();
            }
            ImGui::BeginChild("friends", ImVec2(0, 0), ImGuiChildFlags_None);
            const auto list = c.friends();
            bool any = false;
            for (int pass = 0; pass < 2; ++pass) {
                for (const Client::Friend& f : list) {
                    if (f.relation != Client::Relation::Friend) continue;
                    if ((pass == 0) != f.presence.online()) continue;
                    any = true;
                    ImGui::PushID(int(f.xuid & 0x7FFFFFFF));
                    ImGui::PushID(int(f.xuid >> 32));
                    ImGui::BeginChild("row", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
                    ImGui::TextColored(f.presence.online() ? kGreen : kDim, "%s", f.gamertag.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("- %s", PresenceLine(f.presence).c_str());
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f * scale +
                                    ImGui::GetCursorPosX());
                    ImGui::BeginDisabled(!can_invite || !f.presence.online());
                    if (ImGui::SmallButton("Invite")) {
                        Issue(c.SendInvite(mine.session_id, f.xuid), "Invite " + f.gamertag);
                        Push("Invitation sent to " + f.gamertag);
                    }
                    ImGui::EndDisabled();
                    ImGui::EndChild();
                    ImGui::PopID();
                    ImGui::PopID();
                }
            }
            for (const Client::Friend& f : list) {
                if (f.relation != Client::Relation::RequestReceived) continue;
                any = true;
                ImGui::PushID(int(f.xuid & 0x7FFFFFFF));
                ImGui::PushID(int(f.xuid >> 32));
                ImGui::BeginChild("req", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
                ImGui::Text("%s", f.gamertag.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("wants to be your friend");
                if (ImGui::SmallButton("Accept")) Issue(c.AddFriend(f.xuid), "Accept " + f.gamertag);
                ImGui::SameLine();
                if (ImGui::SmallButton("Decline")) Issue(c.RemoveFriend(f.xuid), "Decline " + f.gamertag);
                ImGui::EndChild();
                ImGui::PopID();
                ImGui::PopID();
            }
            if (!any) ImGui::TextDisabled("No friends yet. Add them from the launcher.");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        const auto inbox = c.invites();
        const std::string invites_label = inbox.empty() ? "Invites" : "Invites (" + std::to_string(inbox.size()) + ")";
        if (ImGui::BeginTabItem(invites_label.c_str())) {
            ImGui::SameLine(size.x - 110.0f * scale);
            ImGui::BeginDisabled(refresh_ticket != 0);
            if (ImGui::SmallButton("Refresh")) refresh_ticket = c.RefreshInvites();
            ImGui::EndDisabled();
            ImGui::BeginChild("invites", ImVec2(0, 0), ImGuiChildFlags_None);
            if (inbox.empty()) ImGui::TextDisabled("Nothing waiting.");
            for (const Client::Invite& invite : inbox) {
                ImGui::PushID(int(invite.id));
                ImGui::BeginChild("inv", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
                ImGui::Text("%s", invite.from_gamertag.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("invited you to their game");
                // Accepting tells the server, which tells THIS game (invite_taken):
                // the port then joins, exactly as if the launcher had accepted.
                if (ImGui::SmallButton("Accept")) {
                    Issue(c.AcceptInvite(invite.id), "Accept");
                    open.store(false);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Decline")) Issue(c.DeclineInvite(invite.id), "Decline");
                ImGui::EndChild();
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        // XLIVE_OVERLAY_TAB=achievements|invites selects a tab on the first
        // frame, for a headless photograph; nobody at a keyboard needs it.
        static const char* wanted_tab = std::getenv("XLIVE_OVERLAY_TAB");
        static bool tab_selected = false;
        const auto select = [&](const char* name) {
            const bool pick = wanted_tab && !tab_selected && std::strcmp(wanted_tab, name) == 0;
            return pick ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        };
        if (ImGui::BeginTabItem("Achievements", nullptr, select("achievements"))) {
            DrawAchievements(c, size.x);
            ImGui::EndTabItem();
        }
        tab_selected = true;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Overlay::Impl::DrawAchievements(Client& c, float width) {
    const uint32_t id = title_id.load();
    if (id == 0) {
        ImGui::TextDisabled("No title.");
        return;
    }
    // Read once per opening, and again after an unlock; never per frame.
    if (title_stale && !title_ticket) {
        title_ticket = c.LoadTitle(id);
        title_stale = false;
    }
    ImGui::SameLine(width - 110.0f * scale);
    ImGui::BeginDisabled(title_ticket != 0);
    if (ImGui::SmallButton("Refresh")) title_stale = true;
    ImGui::EndDisabled();

    if (!title_loaded) {
        if (title_ticket) {
            ImGui::TextDisabled("loading...");
        } else if (!title_error.empty()) {
            ImGui::TextDisabled("%s", title_error.c_str());
            if (title_error == "no_title") ImGui::TextDisabled("This server has not imported the title.");
        }
        return;
    }
    const Client::TitleInfo& info = title.title;

    // The server's word, plus what this client unlocked since and has not
    // yet delivered: an achievement earned a second ago is unlocked here
    // whether or not the queue has drained.
    unsigned unlocked_count = 0;
    for (const auto& a : info.achievements) unlocked_count += a.unlocked || c.IsUnlocked(a.id);
    ImGui::Text("%u of %zu unlocked", unlocked_count, info.achievements.size());
    ImGui::SameLine();
    ImGui::TextDisabled("%u / %u G", c.title_gamerscore(), info.max_gamerscore);
    ImGui::Separator();

    ImGui::BeginChild("achievements", ImVec2(0, 0), ImGuiChildFlags_None);
    const float tile = 56.0f * scale;
    for (const auto& a : info.achievements) {
        const bool unlocked = a.unlocked || c.IsUnlocked(a.id);
        const bool secret = a.hidden && !unlocked;
        ImGui::PushID(a.id);
        ImGui::BeginChild("row", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        auto found = tiles.find(a.image_id);
        if (!secret && found != tiles.end()) {
            const ImVec4 tint = unlocked ? ImVec4(1, 1, 1, 1) : ImVec4(0.5f, 0.5f, 0.5f, 0.75f);
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(found->second.descriptor), ImVec2(tile, tile),
                               ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), tint);
        } else {
            ImGui::Dummy(ImVec2(tile, tile));
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                ImGui::GetColorU32(ImGuiCol_Border));
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (unlocked) {
            ImGui::TextColored(kGreen, "%s", a.name.c_str());
        } else {
            ImGui::TextDisabled("%s", secret ? "Secret achievement" : a.name.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%u G", a.score);
        ImGui::PushTextWrapPos(width - tile - 60.0f * scale);
        if (unlocked) {
            ImGui::TextUnformatted(a.unlocked_description.empty() ? a.locked_description.c_str()
                                                                  : a.unlocked_description.c_str());
            if (!a.unlocked_at.empty()) ImGui::TextDisabled("unlocked %s", a.unlocked_at.substr(0, 10).c_str());
        } else if (secret) {
            ImGui::TextDisabled("Keep playing to reveal it.");
        } else {
            ImGui::TextUnformatted(a.locked_description.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// -- the frame ------------------------------------------------------------------

bool Overlay::Render(const VulkanHandles& handles, VkCommandBuffer cmd, VkImage image,
                     uint32_t width, uint32_t height, uint64_t generation) {
    Impl& s = *impl_;
    Client* client = s.client.load();
    if (!client) return false;

    if (!s.initialized) {
        s.Init(handles);
        if (!s.initialized) return false;
        s.generation = generation;
    }
    if (generation != s.generation || handles.color_format != s.handles.color_format) {
        vkDeviceWaitIdle(s.handles.device);
        s.DropViews();
        if (handles.color_format != s.handles.color_format) {
            ImGui_ImplVulkan_Shutdown();
            ImGui::DestroyContext();
            s.initialized = false;
            s.Init(handles);
            if (!s.initialized) return false;
        }
        s.generation = generation;
    }

    // Events and results are taken every frame — a toast must not wait for
    // the panel to open — but ImGui itself is only touched on a frame that
    // draws something. The common frame, closed with nothing to say, costs
    // a few atomic loads and an empty queue swap.
    s.DrainEvents(*client);
    s.PollTickets(*client);
    {
        const double now = Impl::Now();
        std::erase_if(s.toasts, [&](const Toast& t) { return t.expires_at <= now; });
    }
    const bool is_open = s.open.load();
    if (!is_open && s.toasts.empty()) {
        s.was_open = false;
        s.last_frame = std::chrono::steady_clock::now();
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - s.last_frame).count();
    s.last_frame = now;
    io.DeltaTime = std::clamp(dt, 1.0f / 1000.0f, 0.25f);
    io.DisplaySize = ImVec2(float(width), float(height));
    s.scale = std::clamp(float(height) / 720.0f, 1.0f, 3.0f);
    io.FontGlobalScale = s.scale;

    if (is_open && !s.was_open) {
        // Opening: read what the panel needs once, not per frame.
        s.presence_ticket = client->LoadMyPresence();
        s.refresh_ticket = client->RefreshInvites();
        s.title_stale = true;
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    s.was_open = is_open;

    // Input is queued into io BEFORE NewFrame, which is where ImGui applies
    // it; after would be a frame late.
    s.DrainInput(width, height);
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (is_open) s.DrawPanel(*client, width, height);
    s.DrawToasts(width, height);
    ImGui::Render();
    s.wants_text.store(is_open && io.WantTextInput);

    ImDrawData* draw = ImGui::GetDrawData();
    if (!draw || draw->CmdListsCount == 0 || draw->TotalVtxCount == 0) return false;

    VkImageView view = s.ViewFor(image);
    if (!view) return false;

    VkImageMemoryBarrier to_color{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    to_color.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    to_color.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    to_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_color.image = image;
    to_color.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &to_color);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, {width, height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);
    ImGui_ImplVulkan_RenderDrawData(draw, cmd);
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier back = to_color;
    back.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    back.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    back.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    back.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &back);
    return true;
}

}  // namespace xlive_overlay
