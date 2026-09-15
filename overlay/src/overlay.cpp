#include "xlive_overlay/overlay.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <xlive/client.h>

#include "imgui.h"
#include "i18n.h"
#include "presence_announcer.h"
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
using xlive::i18n::T;

struct Toast {
    std::string text;
    double expires_at = 0;
    // A port's own notice (Overlay::Notify) carries a tag so the port can
    // take it down early (Dismiss) — a co-op call that was answered, say.
    std::string tag;
    // An unlock is the one toast with a picture: the tile, the name, the
    // score and the description, the way the console's popup had them.
    bool achievement = false;
    uint32_t image_id = 0;
    std::string name;
    std::string description;
    uint32_t score = 0;
};

struct PendingSocial {
    Client::Ticket ticket = 0;
    std::string what;
};

std::string PresenceLine(const Client::Presence& p) {
    switch (p.state) {
        case Client::PresenceState::Offline: return T("offline");
        case Client::PresenceState::Online:  return T("online");
        case Client::PresenceState::Playing: {
            std::string line = T("playing ") + (p.title_name.empty() ? T("a game") : p.title_name);
            if (!p.rich_text.empty() && p.rich_text != p.title_name) line += " - " + p.rich_text;
            if (p.joinable) line += T(" (joinable)");
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
    // Notices from the port (Overlay::Notify / Dismiss), any thread; drained
    // into `toasts` on the render thread beside the events.
    struct Notice { std::string text; double seconds; std::string tag; bool dismiss; };
    std::deque<Notice> notices;
    std::mutex notices_mutex;
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
    xlive::theme::PresenceAnnouncer presence_news;

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

    // The messages tab: who has written (the inbox, read when the panel
    // opens and again when a message arrives), one open conversation, and
    // a line to reply on. Unread counts are per sender until opened.
    Client::Ticket inbox_ticket = 0;
    std::vector<Client::Message> inbox;
    uint64_t peer = 0;
    std::string peer_gamertag;
    Client::Ticket conversation_ticket = 0;
    std::vector<Client::Message> conversation;
    Client::Ticket send_ticket = 0;
    char draft[1024] = {};
    std::map<uint64_t, int> unread;
    bool scroll_to_end = false;
    bool focus_draft = false;
    void OpenConversation(Client& c, uint64_t xuid, const std::string& gamertag);
    void DrawMessages(Client& c, float width);

    // Our own clock for the toasts, not ImGui's: a toast can arrive on a
    // frame where ImGui is never touched.
    static double Now() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    // Notifications over the game can be switched off from the panel; the
    // choice is kept in <data dir>/overlay.cfg. Off means nothing pops
    // while playing — invitations and messages still wait in the panel
    // with a count on their tab. What a player just did in the panel
    // ("Invitation sent", "Message not sent") is answered regardless.
    bool notifications = true;
    bool settings_loaded = false;
    // The client at Init time, for the language (launcher.json lives in
    // its data directory).
    Client* client_for_init = nullptr;
    std::filesystem::path settings_path;
    void LoadSettings(Client& c);
    void SaveSettings();
    // The add-a-friend box on the Friends tab.
    char add_gamertag[32] = {};
    // The tab bar, as a number, so a shoulder button can step it: 0
    // Friends, 1 Invites, 2 Messages, 3 Achievements. requested_tab is
    // applied on the next BeginTabItem of that tab and cleared.
    int current_tab = 0;
    int requested_tab = -1;
    static constexpr int kTabs = 4;
    // Opened with the pad: the nav cursor is shown at once, so the first
    // thing on screen is already the thing a press would hit.
    std::atomic<bool> opened_by_pad{false};

    void Push(std::string text, double seconds = 5.0, bool even_if_muted = false) {
        if (!notifications && !even_if_muted) return;
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

void Overlay::Notify(std::string text, double seconds, std::string tag) {
    std::lock_guard<std::mutex> lock(impl_->notices_mutex);
    impl_->notices.push_back({std::move(text), seconds, std::move(tag), false});
    while (impl_->notices.size() > 32) impl_->notices.pop_front();
}
void Overlay::Dismiss(const std::string& tag) {
    std::lock_guard<std::mutex> lock(impl_->notices_mutex);
    impl_->notices.push_back({{}, 0.0, tag, true});
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
            impl_->opened_by_pad.store(impl_->open.load());
            return true;
        }
        if (!impl_->pad_back && !impl_->pad_start) impl_->chord_fired = false;
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE && down) {
            Toggle();
            impl_->opened_by_pad.store(impl_->open.load());
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
    // The launcher's language: XLIVE_LANGUAGE in the environment (the
    // launcher sets it for the games it starts), else launcher.json in the
    // data directory (a game started by hand), else the system's. Japanese
    // and Korean draw from the subset compiled in; a system CJK font, when
    // there is one, covers what friends type.
    {
        namespace i18n = xlive::i18n;
        i18n::Lang lang = i18n::Lang::En;
        bool chosen = false;
        if (const char* env = std::getenv("XLIVE_LANGUAGE"); env && *env) {
            lang = i18n::FromCode(env);
            chosen = true;
        } else if (client_for_init) {
            std::ifstream in(std::filesystem::path(client_for_init->data_dir()) / "launcher.json");
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (const auto at = text.find("\"language\""); at != std::string::npos) {
                const auto open = text.find('"', text.find(':', at) + 1);
                const auto close = open == std::string::npos ? open : text.find('"', open + 1);
                if (close != std::string::npos) {
                    lang = i18n::FromCode(text.substr(open + 1, close - open - 1));
                    chosen = true;
                }
            }
        }
        if (!chosen) lang = i18n::FromSystem();
        i18n::Set(lang);
        // The game's own words for its achievements, in this language.
        if (client_for_init) client_for_init->SetLanguage(lang == i18n::Lang::En ? "" : i18n::Info(lang).code);
        const std::string cjk = i18n::Info(lang).cjk ? i18n::FindCjkFont(lang) : std::string();
        fonts = xlive::theme::LoadFonts(io, 20.0f, nullptr, cjk.empty() ? nullptr : cjk.c_str(),
                                        i18n::Info(lang).cjk ? i18n::Info(lang).code : nullptr);
    }

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
                // A game window keeps SDL's text input off (composed text
                // through the IME lags raw keys), and a port that does not
                // turn it on for us sends no SDL_TEXTINPUT at all. Then the
                // key itself is the character: the keysym is the layout's
                // own unshifted symbol, shifted here for the US-ish cases.
                // Enough for a gamertag or a message; a port that does turn
                // text input on (Case West) delivers the real thing and
                // this stays out of the way.
                if (e.type == SDL_KEYDOWN && !SDL_IsTextInputActive() && io.WantTextInput &&
                    !(mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))) {
                    const SDL_Keycode sym = e.key.keysym.sym;
                    if (sym >= 0x20 && sym < 0x7F) {
                        char c = char(sym);
                        const bool shift = (mod & KMOD_SHIFT) != 0;
                        const bool caps = (mod & KMOD_CAPS) != 0;
                        if (c >= 'a' && c <= 'z') {
                            if (shift != caps) c = char(c - 'a' + 'A');
                        } else if (shift) {
                            static const char* from = "1234567890-=[]\;',./`";
                            static const char* to = "!@#$%^&*()_+{}|:\"<>?~";
                            if (const char* at = std::strchr(from, c)) c = to[at - from];
                        }
                        const char text[2] = {c, 0};
                        io.AddInputCharactersUTF8(text);
                    }
                }
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
                } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX) {
                    // The right stick scrolls a list, as in ImGui's own nav.
                    analog(ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight);
                } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
                    analog(ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown);
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
            case xlive::EventKind::AchievementUnlocked: {
                title_stale = true;
                Toast t;
                t.achievement = true;
                t.name = event.achievement_name.empty() ? "#" + std::to_string(event.achievement_id)
                                                        : event.achievement_name;
                t.score = event.score;
                // The description and the tile come from the definitions,
                // read at start so they are here by the time anything is
                // earned; a tile not on the GPU yet is asked for now and
                // drawn the moment it lands.
                for (const auto& a : title.title.achievements) {
                    if (a.id != event.achievement_id) continue;
                    if (!a.name.empty()) t.name = a.name;
                    t.description = a.unlocked_description;
                    if (a.score) t.score = a.score;
                    t.image_id = a.image_id;
                    if (t.image_id && !tiles.count(t.image_id) && !tiles_missing.count(t.image_id) &&
                        !image_tickets.count(t.image_id)) {
                        image_tickets[t.image_id] = c.LoadImage(title.title.title_id, t.image_id);
                    }
                    break;
                }
                t.text = T("Achievement unlocked: ") + t.name;
                t.expires_at = Now() + 9.0;
                if (notifications) {
                    toasts.push_back(std::move(t));
                    while (toasts.size() > 5) toasts.erase(toasts.begin());
                }
                break;
            }
            case xlive::EventKind::InviteReceived:
                Push(event.gamertag + T(" invited you to play - Shift+Tab to answer"), 12.0);
                break;
            case xlive::EventKind::InviteAccepted:
                Push(T("Joining ") + event.gamertag + T("'s game"), 6.0);
                break;
            case xlive::EventKind::MessageReceived:
                if (open.load() && peer == event.xuid) {
                    if (!conversation_ticket) conversation_ticket = c.LoadConversation(peer);
                } else {
                    unread[event.xuid] += 1;
                }
                if (open.load() && !inbox_ticket) inbox_ticket = c.LoadConversations();
                Push(event.gamertag + ": " + event.message + T("  -  Shift+Tab to reply"), 10.0);
                break;
            case xlive::EventKind::InviteAnswered:
                Push(event.gamertag + (event.accepted ? T(" accepted") : T(" declined")) +
                     T(" your invitation"));
                break;
            case xlive::EventKind::SigninChanged:
                // Signing in is not news over a game the launcher just
                // started as that account; the panel's header says who.
                // Being signed OUT mid-game is.
                if (!c.identity().online()) Push(T("Signed out of XenonLive"));
                break;
            case xlive::EventKind::ConnectionChanged:
                if (!c.online()) Push(T("XenonLive connection lost; retrying"));
                break;
            case xlive::EventKind::FriendsChanged: {
                const auto now = c.friends();
                if (friends_baseline) {
                    for (const auto& f : now) {
                        const Client::Friend* was = nullptr;
                        for (const auto& old : last_friends) {
                            if (old.xuid == f.xuid) was = &old;
                        }
                        if (f.relation == Client::Relation::RequestReceived &&
                            (!was || was->relation != Client::Relation::RequestReceived)) {
                            Push(f.gamertag + T(" wants to be your friend"));
                        }
                    }
                }
                for (const auto& line : presence_news.Update(now, Now())) {
                    Push(line.gamertag + " " + line.text);
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
        if (status == Client::OpStatus::Failed) Push(pending[i].what + T(" failed: ") + result.error, 5.0, true);
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
    if (inbox_ticket) {
        Client::SocialResult result;
        const auto status = c.Poll(inbox_ticket, result);
        if (status != Client::OpStatus::Pending) {
            inbox_ticket = 0;
            if (status == Client::OpStatus::Succeeded) inbox = std::move(result.messages);
        }
    }
    if (conversation_ticket) {
        Client::SocialResult result;
        const auto status = c.Poll(conversation_ticket, result);
        if (status != Client::OpStatus::Pending) {
            conversation_ticket = 0;
            if (status == Client::OpStatus::Succeeded) {
                conversation = std::move(result.messages);
                scroll_to_end = true;
            }
        }
    }
    if (send_ticket) {
        Client::SocialResult result;
        const auto status = c.Poll(send_ticket, result);
        if (status != Client::OpStatus::Pending) {
            send_ticket = 0;
            if (status == Client::OpStatus::Succeeded) {
                std::memset(draft, 0, sizeof(draft));
                focus_draft = true;
                if (!result.messages.empty()) {
                    conversation.push_back(result.messages.front());
                    while (conversation.size() > Client::kMessagesKept) conversation.erase(conversation.begin());
                    scroll_to_end = true;
                }
                if (!inbox_ticket) inbox_ticket = c.LoadConversations();
            } else {
                Push(T("Message not sent: ") + result.error, 5.0, true);
            }
        }
    }
    // The definitions are read as soon as the client is online — not on the
    // first opening of the panel — so an unlock's popup has its description
    // and tile even for a player who never presses Shift+Tab.
    if (title_stale && !title_ticket && !title_loaded && c.online() && title_id.load() != 0) {
        title_ticket = c.LoadTitle(title_id.load());
        title_stale = false;
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
                // XLIVE_OVERLAY_FAKE_UNLOCK=<id>: the unlock popup for that
                // achievement, for a photograph; the server learns nothing.
                if (const char* fake = std::getenv("XLIVE_OVERLAY_FAKE_UNLOCK")) {
                    static bool once = false;
                    if (!once) {
                        once = true;
                        xlive::Event e;
                        e.kind = xlive::EventKind::AchievementUnlocked;
                        e.achievement_id = uint16_t(std::strtoul(fake, nullptr, 10));
                        std::lock_guard<std::mutex> lock(events_mutex);
                        events.push_back(e);
                    }
                }
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

// -- settings -----------------------------------------------------------------

void Overlay::Impl::LoadSettings(Client& c) {
    settings_loaded = true;
    // One line per setting, "name=value": nothing to parse but a split.
    settings_path = std::filesystem::path(c.data_dir()) / "overlay.cfg";
    std::ifstream in(settings_path);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "notifications") notifications = value != "0";
    }
    // XLIVE_OVERLAY_NOTIFICATIONS=0 overrides the file for one run.
    if (const char* env = std::getenv("XLIVE_OVERLAY_NOTIFICATIONS")) notifications = std::string(env) != "0";
}

void Overlay::Impl::SaveSettings() {
    if (settings_path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(settings_path.parent_path(), ec);
    std::ofstream out(settings_path, std::ios::trunc);
    if (out) out << "notifications=" << (notifications ? 1 : 0) << "\n";
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
        // One window per toast: PushID does not reach a window's name, and
        // a shared "##toast" would fold every toast into the first one's box.
        char name[24];
        std::snprintf(name, sizeof(name), "##toast%zu", i);
        ImGui::PushID(int(i));
        if (ImGui::Begin(name, nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoInputs)) {
            const ImVec2 min = ImGui::GetWindowPos();
            const ImVec2 max(min.x + ImGui::GetWindowSize().x, min.y + ImGui::GetWindowSize().y);
            ImGui::GetWindowDrawList()->AddRectFilled(
                min, ImVec2(min.x + 5.0f * scale, max.y), ImGui::GetColorU32(xlive::theme::kLime),
                ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersLeft);
            const Toast& t = toasts[i];
            if (t.achievement) {
                // The tile on the left (a bordered square until it lands),
                // "Achievement unlocked" in lime, the name big, the score,
                // the description under it.
                const float tile = 64.0f * scale;
                auto found = t.image_id ? tiles.find(t.image_id) : tiles.end();
                if (found != tiles.end()) {
                    ImGui::Image(reinterpret_cast<ImTextureID>(found->second.descriptor), ImVec2(tile, tile));
                } else {
                    ImGui::Dummy(ImVec2(tile, tile));
                    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                        ImGui::GetColorU32(ImGuiCol_Border));
                }
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextColored(xlive::theme::kLime, T("Achievement unlocked"));
                ImGui::PushFont(fonts.heading);
                ImGui::TextUnformatted(t.name.c_str());
                ImGui::PopFont();
                if (t.score) {
                    // The score is body-size beside a heading-size name:
                    // dropped so the two sit on one baseline rather than
                    // one top edge.
                    ImGui::SameLine();
                    const float drop = fonts.heading->Ascent - ImGui::GetFont()->Ascent;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + drop);
                    ImGui::TextDisabled(T("%u G"), t.score);
                }
                if (!t.description.empty()) {
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330.0f * scale);
                    ImGui::TextColored(xlive::theme::kMuted, "%s", t.description.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::EndGroup();
            } else {
                ImGui::PushTextWrapPos(440.0f * scale);
                ImGui::TextUnformatted(t.text.c_str());
                ImGui::PopTextWrapPos();
            }
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
    ImGui::TextColored(c.online() ? kGreen : kAmber, c.online() ? T("online") : T("offline"));
    const char* hint = T("Shift+Tab / View+Menu closes  -  LB RB tabs, B back");
    const float toggle_w = ImGui::CalcTextSize(T("Notifications")).x + ImGui::GetFrameHeight() + 8.0f * scale;
    // The hint sits between the name and the switch when it fits — a
    // language's longer wording, or a long gamertag, drops it rather than
    // overprint the name.
    const float hint_x = size.x - ImGui::CalcTextSize(hint).x - toggle_w - 20.0f * scale -
                         ImGui::GetStyle().WindowPadding.x;
    const float name_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    if (hint_x > name_end + 24.0f * scale) {
        ImGui::SameLine(hint_x);
        ImGui::TextDisabled("%s", hint);
    }
    ImGui::SameLine(size.x - toggle_w - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Checkbox(T("Notifications"), &notifications)) SaveSettings();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(T("Off: nothing pops up over the game. Invitations and messages still\nwait here, with a count on their tab."));
    }
    ImGui::Separator();

    // The pad: LB/RB step the tabs, B closes (unless a text box has the
    // focus, where B is its cancel). XLIVE_OVERLAY_TAB=friends|invites|
    // messages|achievements picks one on the first frame, for a photograph.
    {
        static const char* wanted_tab = std::getenv("XLIVE_OVERLAY_TAB");
        if (wanted_tab) {
            const char* names[kTabs] = {"friends", "invites", "messages", "achievements"};
            for (int i = 0; i < kTabs; ++i) {
                if (std::strcmp(wanted_tab, names[i]) == 0) requested_tab = i;
            }
            wanted_tab = nullptr;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) requested_tab = (current_tab + kTabs - 1) % kTabs;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) requested_tab = (current_tab + 1) % kTabs;
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) && !ImGui::GetIO().WantTextInput) {
            open.store(false);
        }
        if (opened_by_pad.exchange(false)) ImGui::SetNavCursorVisible(true);
    }
    const auto tab_flags = [&](int index) {
        return requested_tab == index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    };

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem(T("Friends"), nullptr, tab_flags(0))) {
            current_tab = 0;
            const bool can_invite = mine_loaded && mine.session_id != 0;
            if (presence_ticket) {
                ImGui::TextDisabled(T("reading your session..."));
            } else if (can_invite) {
                ImGui::TextDisabled(T("in a session: friends can be invited"));
            } else {
                ImGui::TextDisabled(T("not in a joinable session yet"));
            }
            ImGui::SameLine(size.x - 110.0f * scale);
            if (ImGui::SmallButton(T("Refresh"))) {
                Issue(c.RefreshFriends(), "Refresh");
                if (!presence_ticket) presence_ticket = c.LoadMyPresence();
            }
            // Add by gamertag, as on the launcher's Friends tab. Asking
            // someone who has asked you IS accepting, so one box does both.
            ImGui::SetNextItemWidth(220.0f * scale);
            const bool add_enter = ImGui::InputTextWithHint("##add", T("gamertag"), add_gamertag,
                                                            sizeof(add_gamertag),
                                                            ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if ((ImGui::SmallButton(T("Add friend")) || add_enter) && add_gamertag[0] != '\0') {
                const std::string tag = add_gamertag;
                Issue(c.AddFriendByGamertag(tag), T("Add ") + tag);
                Push(T("Friend request sent to ") + tag, 5.0, true);
                std::memset(add_gamertag, 0, sizeof(add_gamertag));
            }
            ImGui::BeginChild("friends", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
            const auto list = c.friends();
            bool any = false;
            for (int pass = 0; pass < 2; ++pass) {
                for (const Client::Friend& f : list) {
                    if (f.relation != Client::Relation::Friend) continue;
                    if ((pass == 0) != f.presence.online()) continue;
                    any = true;
                    ImGui::PushID(int(f.xuid & 0x7FFFFFFF));
                    ImGui::PushID(int(f.xuid >> 32));
                    ImGui::BeginChild("row", ImVec2(0, 0), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
                    ImGui::TextColored(f.presence.online() ? kGreen : kDim, "%s", f.gamertag.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("- %s", PresenceLine(f.presence).c_str());
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f * scale +
                                    ImGui::GetCursorPosX());
                    ImGui::BeginDisabled(!can_invite || !f.presence.online());
                    if (ImGui::SmallButton(T("Invite"))) {
                        Issue(c.SendInvite(mine.session_id, f.xuid), T("Invite ") + f.gamertag);
                        Push(T("Invitation sent to ") + f.gamertag, 5.0, true);
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
                ImGui::BeginChild("req", ImVec2(0, 0), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
                ImGui::Text("%s", f.gamertag.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled(T("wants to be your friend"));
                if (ImGui::SmallButton(T("Accept"))) Issue(c.AddFriend(f.xuid), T("Accept ") + f.gamertag);
                ImGui::SameLine();
                if (ImGui::SmallButton(T("Decline"))) Issue(c.RemoveFriend(f.xuid), T("Decline ") + f.gamertag);
                ImGui::EndChild();
                ImGui::PopID();
                ImGui::PopID();
            }
            if (!any) ImGui::TextDisabled(T("No friends yet. Type a gamertag above to add one."));
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        const auto invites = c.invites();
        // "###" keeps the tab's identity while the count in its label
        // changes; without it a new count is a new tab, and the selection
        // jumps back to the first one.
        const std::string invites_label =
            (invites.empty() ? std::string(T("Invites")) : T("Invites (") + std::to_string(invites.size()) + ")") + "###invites";
        if (ImGui::BeginTabItem(invites_label.c_str(), nullptr, tab_flags(1))) {
            current_tab = 1;
            ImGui::SameLine(size.x - 110.0f * scale);
            ImGui::BeginDisabled(refresh_ticket != 0);
            if (ImGui::SmallButton(T("Refresh"))) refresh_ticket = c.RefreshInvites();
            ImGui::EndDisabled();
            ImGui::BeginChild("invites", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
            if (invites.empty()) ImGui::TextDisabled(T("Nothing waiting."));
            for (const Client::Invite& invite : invites) {
                ImGui::PushID(int(invite.id));
                ImGui::BeginChild("inv", ImVec2(0, 0), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
                ImGui::Text("%s", invite.from_gamertag.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled(T("invited you to their game"));
                // Accepting tells the server, which tells THIS game (invite_taken):
                // the port then joins, exactly as if the launcher had accepted.
                if (ImGui::SmallButton(T("Accept"))) {
                    Issue(c.AcceptInvite(invite.id), "Accept");
                    open.store(false);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(T("Decline"))) Issue(c.DeclineInvite(invite.id), "Decline");
                ImGui::EndChild();
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        int unread_total = 0;
        for (const auto& [xuid, n] : unread) unread_total += n;
        const std::string messages_label =
            (unread_total ? T("Messages (") + std::to_string(unread_total) + ")" : std::string(T("Messages"))) + "###messages";
        if (ImGui::BeginTabItem(messages_label.c_str(), nullptr, tab_flags(2))) {
            current_tab = 2;
            DrawMessages(c, size.x);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(T("Achievements"), nullptr, tab_flags(3))) {
            current_tab = 3;
            DrawAchievements(c, size.x);
            ImGui::EndTabItem();
        }
        requested_tab = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Overlay::Impl::OpenConversation(Client& c, uint64_t xuid, const std::string& gamertag) {
    if (peer != xuid) {
        std::memset(draft, 0, sizeof(draft));
        conversation.clear();
    }
    peer = xuid;
    peer_gamertag = gamertag;
    unread.erase(xuid);
    if (!conversation_ticket) conversation_ticket = c.LoadConversation(xuid);
    focus_draft = true;
}

void Overlay::Impl::DrawMessages(Client& c, float width) {
    const uint64_t me = c.identity().xuid;
    const auto friends = c.friends();
    const auto find_friend = [&](uint64_t xuid) -> const Client::Friend* {
        for (const auto& f : friends) if (f.xuid == xuid) return &f;
        return nullptr;
    };

    // People on the left: those with a conversation, then the other friends.
    ImGui::BeginChild("people", ImVec2(width * 0.3f, 0), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_Borders);
    std::vector<uint64_t> listed;
    const auto person = [&](uint64_t xuid, const std::string& name, const Client::Message* latest) {
        ImGui::PushID(int(xuid & 0x7FFFFFFF));
        ImGui::PushID(int(xuid >> 32));
        std::string label = name;
        const auto n = unread.find(xuid);
        if (n != unread.end() && n->second > 0) label += " (" + std::to_string(n->second) + ")";
        if (ImGui::Selectable(label.c_str(), peer == xuid)) OpenConversation(c, xuid, name);
        if (latest) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s%s", latest->from_xuid == xuid ? "" : T("You: "),
                                latest->body.substr(0, 40).c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::PopID();
        ImGui::PopID();
    };
    for (const Client::Message& latest : inbox) {
        const uint64_t other = latest.from_xuid == me ? latest.to_xuid : latest.from_xuid;
        listed.push_back(other);
        person(other, latest.from_xuid == me ? latest.to_gamertag : latest.from_gamertag, &latest);
    }
    for (const auto& f : friends) {
        if (!f.is_friend()) continue;
        if (std::find(listed.begin(), listed.end(), f.xuid) != listed.end()) continue;
        person(f.xuid, f.gamertag, nullptr);
    }
    if (listed.empty() && friends.empty()) ImGui::TextDisabled(T("No friends yet."));
    ImGui::EndChild();
    ImGui::SameLine();

    // XLIVE_OVERLAY_MESSAGE=<gamertag> opens that conversation, for a
    // headless photograph.
    static const char* wanted_peer = std::getenv("XLIVE_OVERLAY_MESSAGE");
    if (wanted_peer && peer == 0) {
        for (const auto& f : friends) {
            if (f.gamertag == wanted_peer) OpenConversation(c, f.xuid, f.gamertag);
        }
        wanted_peer = nullptr;
    }

    ImGui::BeginGroup();
    if (peer == 0) {
        ImGui::TextDisabled(T("Pick someone. A message is up to 256 characters;"));
        ImGui::TextDisabled(T("the last 20 between you are kept."));
        ImGui::EndGroup();
        return;
    }
    const Client::Friend* f = find_friend(peer);
    ImGui::PushFont(fonts.heading);
    ImGui::Text("%s", peer_gamertag.c_str());
    ImGui::PopFont();
    if (f && f->is_friend()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", PresenceLine(f->presence).c_str());
    }
    const float input_h = ImGui::GetFrameHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    ImGui::BeginChild("log", ImVec2(0, ImGui::GetContentRegionAvail().y - input_h), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_Borders);
    if (conversation.empty() && !conversation_ticket) ImGui::TextDisabled(T("No messages yet."));
    for (const Client::Message& msg : conversation) {
        const bool from_me = msg.from_xuid == me;
        ImGui::PushID(int(msg.id));
        ImGui::TextColored(from_me ? kGreen : ImVec4(0.75f, 0.8f, 0.9f, 1.0f), "%s",
                           from_me ? T("You") : msg.from_gamertag.c_str());
        if (msg.sent_at.size() >= 16) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s %s", msg.sent_at.substr(5, 5).c_str(), msg.sent_at.substr(11, 5).c_str());
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(msg.body.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::PopID();
    }
    if (scroll_to_end) {
        ImGui::SetScrollHereY(1.0f);
        scroll_to_end = false;
    }
    ImGui::EndChild();

    size_t length = 0;
    for (const char* ch = draft; *ch; ++ch) length += (uint8_t(*ch) & 0xC0) != 0x80;
    const bool over = length > Client::kMaxMessageLength;
    const bool can_write = f && f->is_friend() && !send_ticket;
    ImGui::BeginDisabled(!can_write);
    if (focus_draft) {
        ImGui::SetKeyboardFocusHere();
        focus_draft = false;
    }
    ImGui::SetNextItemWidth(-90.0f * scale);
    const bool enter = ImGui::InputTextWithHint("##draft", T("write a message, Enter sends"), draft,
                                                sizeof(draft), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::BeginDisabled(over || draft[0] == '\0');
    if ((ImGui::Button(T("Send"), ImVec2(-1.0f, 0.0f)) || enter) && !over && draft[0] != '\0') {
        send_ticket = c.SendMessage(peer, draft);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::TextColored(over ? kAmber : kDim, T("%zu / %zu%s"), length, Client::kMaxMessageLength,
                       f && f->is_friend() ? "" : T("  -  not a friend any more; you can read, not write"));
    ImGui::EndGroup();
}

void Overlay::Impl::DrawAchievements(Client& c, float width) {
    const uint32_t id = title_id.load();
    if (id == 0) {
        ImGui::TextDisabled(T("No title."));
        return;
    }
    // Read once per opening, and again after an unlock; never per frame.
    if (title_stale && !title_ticket) {
        title_ticket = c.LoadTitle(id);
        title_stale = false;
    }
    ImGui::SameLine(width - 110.0f * scale);
    ImGui::BeginDisabled(title_ticket != 0);
    if (ImGui::SmallButton(T("Refresh"))) title_stale = true;
    ImGui::EndDisabled();

    if (!title_loaded) {
        if (title_ticket) {
            ImGui::TextDisabled(T("loading..."));
        } else if (!title_error.empty()) {
            ImGui::TextDisabled("%s", title_error.c_str());
            if (title_error == "no_title") ImGui::TextDisabled(T("This server has not imported the title."));
        }
        return;
    }
    const Client::TitleInfo& info = title.title;

    // The server's word, plus what this client unlocked since and has not
    // yet delivered: an achievement earned a second ago is unlocked here
    // whether or not the queue has drained.
    unsigned unlocked_count = 0;
    for (const auto& a : info.achievements) unlocked_count += a.unlocked || c.IsUnlocked(a.id);
    ImGui::Text(T("%u of %zu unlocked"), unlocked_count, info.achievements.size());
    ImGui::SameLine();
    ImGui::TextDisabled(T("%u / %u G"), c.title_gamerscore(), info.max_gamerscore);
    ImGui::Separator();

    ImGui::BeginChild("achievements", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
    const float tile = 56.0f * scale;
    for (const auto& a : info.achievements) {
        const bool unlocked = a.unlocked || c.IsUnlocked(a.id);
        const bool secret = a.hidden && !unlocked;
        ImGui::PushID(a.id);
        ImGui::BeginChild("row", ImVec2(0, 0), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
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
            ImGui::TextDisabled("%s", secret ? T("Secret achievement") : a.name.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled(T("%u G"), a.score);
        ImGui::PushTextWrapPos(width - tile - 60.0f * scale);
        if (unlocked) {
            ImGui::TextUnformatted(a.unlocked_description.empty() ? a.locked_description.c_str()
                                                                  : a.unlocked_description.c_str());
            if (!a.unlocked_at.empty()) ImGui::TextDisabled(T("unlocked %s"), a.unlocked_at.substr(0, 10).c_str());
        } else if (secret) {
            ImGui::TextDisabled(T("Keep playing to reveal it."));
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
        s.client_for_init = client;
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
            s.client_for_init = client;
            s.Init(handles);
            if (!s.initialized) return false;
        }
        s.generation = generation;
    }

    // Events and results are taken every frame — a toast must not wait for
    // the panel to open — but ImGui itself is only touched on a frame that
    // draws something. The common frame, closed with nothing to say, costs
    // a few atomic loads and an empty queue swap.
    if (!s.settings_loaded) s.LoadSettings(*client);
    s.DrainEvents(*client);
    s.PollTickets(*client);
    {
        // The port's own notices: shown whatever the notifications setting
        // says (they are the game asking the player something, not social
        // chatter), replaced in place when the tag is already up, taken down
        // by a Dismiss of the tag.
        std::deque<Impl::Notice> batch;
        {
            std::lock_guard<std::mutex> lock(s.notices_mutex);
            batch.swap(s.notices);
        }
        for (Impl::Notice& n : batch) {
            if (!n.tag.empty())
                std::erase_if(s.toasts, [&](const Toast& t) { return t.tag == n.tag; });
            if (n.dismiss) continue;
            Toast t;
            t.text = std::move(n.text);
            t.tag = std::move(n.tag);
            t.expires_at = Impl::Now() + n.seconds;
            s.toasts.push_back(std::move(t));
            while (s.toasts.size() > 5) s.toasts.erase(s.toasts.begin());
        }
    }
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
        if (!s.inbox_ticket) s.inbox_ticket = client->LoadConversations();
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
