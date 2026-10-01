#include "review.h"
#include "review_lock.h"
#include "embedded_deck.h"

#include <draxul/plugin_adapter.h>
#include <draxul/plugin_host_services.h>
#include <draxul/plugin_nanovg_pass.h>
#include <draxul/input_types.h>
#include <nanovg.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
using namespace draxul::plugin_support;
struct Hit
{
    float x, y, w, h;
    std::string action;
};
struct Instance
{
    explicit Instance(const DraxulPluginCreateInfoV2& info) : services(info) {}
    ~Instance() { pass.reset(); }
    HostServices services;
    DraxulPluginViewportV2 viewport{};
    std::unique_ptr<flashcards::ReviewSession> review;
    std::vector<unsigned char> font;
    std::map<std::string, int> images;
    std::unique_ptr<IPluginNanoVGPass> pass;
    std::vector<Hit> hits;
    float scale = 1;
    bool visible = true, focused = false, quiesced = false;
    bool key_down = false, mouse_down = false;
    bool frame_ready_logged = false;
    std::string pressed_action;
    std::string status;
};

std::vector<unsigned char> read_asset(const std::filesystem::path& path, size_t limit)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Missing asset");
    const auto size = stream.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(limit))
        throw std::runtime_error("Invalid asset size");
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Unreadable asset");
    return bytes;
}
void changed(Instance& instance)
{
    instance.services.request_redraw();
    instance.services.notify_presentation_changed();
}
void act(Instance& instance, std::string_view action)
{
    if (instance.quiesced)
        return;
    instance.services.log(DRAXUL_PLUGIN_LOG_DEBUG, "Flashcards action " + std::string(action)
        + " revealed=" + std::to_string(instance.review->revealed())
        + " image=" + std::to_string(instance.review->image_ready()));
    if (action == "flip") instance.review->flip();
    else if (action == "hint") instance.review->show_hint();
    else if (action == "remembered") instance.review->grade(true);
    else if (action == "forgot") instance.review->grade(false);
    else if (action == "skip") instance.review->skip();
    else if (action == "refresh") instance.review->start_batch();
    changed(instance);
}
void rect(NVGcontext* vg, float x, float y, float w, float h, NVGcolor color, float radius = 12)
{
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillColor(vg, color);
    nvgFill(vg);
}
void label(NVGcontext* vg, float x, float y, float size, const std::string& text, NVGcolor color, int align = NVG_ALIGN_LEFT)
{
    nvgFontFace(vg, "flashcards");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, align | NVG_ALIGN_TOP);
    nvgFillColor(vg, color);
    nvgText(vg, x, y, text.c_str(), nullptr);
}
void paragraph(NVGcontext* vg, float x, float y, float width, float size, const std::string& text, NVGcolor color)
{
    nvgFontFace(vg, "flashcards");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, color);
    nvgTextBox(vg, x, y, width, text.c_str(), nullptr);
}
void button(Instance& instance, NVGcontext* vg, float x, float y, float w,
    const std::string& text, const std::string& action, NVGcolor color)
{
    rect(vg, x, y, w, 44, color);
    label(vg, x + w / 2, y + 9, 17, text, nvgRGB(243, 246, 255), NVG_ALIGN_CENTER);
    instance.hits.push_back({ x, y, w, 44, action });
}
int image(Instance& instance, NVGcontext* vg, const std::string& name)
{
    if (name.empty())
        return -1;
    if (const auto entry = instance.images.find(name); entry != instance.images.end())
        return entry->second;
    int result = -1;
    try
    {
        const auto bytes = read_asset(instance.services.plugin_directory() / "assets" / name, 1024 * 1024);
        int w = 0, h = 0, components = 0;
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &components)
            || w <= 0 || h <= 0 || w > 1024 || h > 1024)
            throw std::runtime_error("Image dimensions out of bounds");
        auto* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &components, 4);
        if (pixels)
        {
            result = nvgCreateImageRGBA(vg, w, h, 0, pixels);
            stbi_image_free(pixels);
        }
    }
    catch (...) {}
    instance.images.emplace(name, result);
    return result;
}
void draw(Instance& instance, NVGcontext* vg, int pixel_w, int pixel_h)
{
    instance.hits.clear();
    if (nvgFindFont(vg, "flashcards") < 0)
    {
        instance.images.clear(); // Native context changed (device/format recreation).
        if (nvgCreateFontMem(vg, "flashcards", instance.font.data(), static_cast<int>(instance.font.size()), 0) < 0)
        {
            instance.services.log(DRAXUL_PLUGIN_LOG_ERROR, "Flashcards Japanese font failed to load");
            return;
        }
    }
    instance.scale = std::max(0.25f, std::min(pixel_w / 720.0f, pixel_h / 660.0f));
    nvgSave(vg);
    nvgScale(vg, instance.scale, instance.scale);
    const float width = pixel_w / instance.scale;
    const float height = pixel_h / instance.scale;
    const float panel = std::min(width - 48, 780.0f), left = (width - panel) / 2;
    const auto white = nvgRGB(234, 241, 255), muted = nvgRGB(163, 181, 206);
    rect(vg, 0, 0, width, height, nvgRGB(14, 23, 39), 0);
    label(vg, left, 22, 24, "Japanese flashcards", white);
    label(vg, left, 58, 14, "Recognition  /  " + std::to_string(instance.review->due_count()) + " due", muted);
    rect(vg, left, 92, panel, 464, nvgRGB(27, 41, 62), 22);
    if (!instance.review->error().empty())
        paragraph(vg, left + 20, 574, panel - 40, 14, instance.review->error(), nvgRGB(255, 197, 121));
    if (const auto* card = instance.review->current())
    {
        const int hint = image(instance, vg, card->image);
        instance.review->set_image_ready(hint > 0);
        const float kana_size = std::min(60.0f, (panel - 56) / std::max<size_t>(1, card->kana.size() / 3));
        label(vg, width / 2, 120, kana_size, card->kana, white, NVG_ALIGN_CENTER);
        if (instance.review->revealed() || instance.review->hinted())
        {
            if (hint > 0)
            {
                const auto paint = nvgImagePattern(vg, width / 2 - 86, 214, 172, 172, 0, hint, 1);
                nvgBeginPath(vg);
                nvgRect(vg, width / 2 - 86, 214, 172, 172);
                nvgFillPaint(vg, paint);
                nvgFill(vg);
            }
            else
            {
                paragraph(vg, left + 32, 254, panel - 64, 19, "No image hint available. Skip this card until a vetted image is added.", muted);
            }
        }
        else
        {
            label(vg, width / 2, 274, 20, "Recall its meaning before you flip", muted, NVG_ALIGN_CENTER);
        }
        if (instance.review->revealed())
        {
            label(vg, width / 2, 398, 18, card->romaji, muted, NVG_ALIGN_CENTER);
            if (hint > 0)
            {
                label(vg, width / 2, 430, 12, card->attribution, muted, NVG_ALIGN_CENTER);
                const float half = (panel - 56) / 2;
                button(instance, vg, left + 20, 470, half, "1  Forgot", "forgot", nvgRGB(132, 63, 66));
                button(instance, vg, left + 36 + half, 470, half,
                    instance.review->hinted() ? "2  Remembered with hint" : "2  Remembered", "remembered", nvgRGB(39, 112, 103));
                label(vg, width / 2, 523, 12, instance.review->hinted() ? "Hint used: retry in 10 minutes; no interval increase" : "Grade your recall. Flipping alone changes no progress.", muted, NVG_ALIGN_CENTER);
            }
            else button(instance, vg, left + 20, 470, panel - 40, "Skip - no grade", "skip", nvgRGB(59, 80, 108));
        }
        else
        {
            button(instance, vg, left + 20, 470, panel - 40, "Space  Flip", "flip", nvgRGB(57, 88, 143));
            if (!instance.review->hinted())
                button(instance, vg, width / 2 - 85, 400, 170, "H  Show hint", "hint", nvgRGB(48, 65, 90));
        }
    }
    else
    {
        label(vg, width / 2, 206, 30, instance.review->blocked() ? "Review unavailable"
            : instance.review->deck_size() == 0 ? "Your deck is empty"
            : instance.review->batch_finished() ? "Session complete"
            : instance.review->due_count() > 0 ? "Images needed for remaining cards" : "All caught up", white, NVG_ALIGN_CENTER);
        std::string next = "New words appear after the next successful build.";
        if (const auto due = instance.review->next_due())
        {
            const auto minutes = std::max<int64_t>(1, (*due - flashcards::unix_now() + 59) / 60);
            next = "Next review in about " + std::to_string(minutes) + " minutes.";
        }
        paragraph(vg, left + 40, 282, panel - 80, 18, next, muted);
        button(instance, vg, left + 20, 470, panel - 40, "Check due cards / retry storage", "refresh", nvgRGB(57, 88, 143));
    }
    label(vg, left, height - 42, 12, "Embedded snapshot " + std::string(flashcards::embedded::fingerprint.substr(0, 12)), muted);
    label(vg, left, height - 24, 11, "Source validated at build time. Offline review; rebuild to refresh.", muted);
    nvgRestore(vg);
    if (!instance.frame_ready_logged)
    {
        instance.services.log(DRAXUL_PLUGIN_LOG_DEBUG, "Flashcards frame ready");
        instance.frame_ready_logged = true;
    }
}

void* create(const DraxulPluginCreateInfoV2* info)
{
    if (!info || info->struct_size < sizeof(*info) || !info->host
        || info->host->struct_size < sizeof(DraxulPluginHostApiV2) || info->host->abi_version != DRAXUL_PLUGIN_ABI_VERSION)
        return nullptr;
    auto instance = std::make_unique<Instance>(*info);
    try
    {
        const auto config = parse_config_json(*info);
        if (!config || !config->is_object() || config->size() != 0)
            throw std::runtime_error("Flashcards expects an empty configuration");
        instance->viewport = info->initial_viewport;
        instance->font = read_asset(instance->services.plugin_directory() / "assets/NotoSansJP.otf", 16 * 1024 * 1024);
        instance->pass = create_plugin_nanovg_pass({ instance->services.plugin_directory() });
        auto* raw = instance.get();
        instance->review = std::make_unique<flashcards::ReviewSession>(flashcards::parse_deck(flashcards::embedded::deck),
            [raw]() -> std::optional<std::string> {
                if (!raw->services.has_storage())
                    throw std::runtime_error("No persistent storage service");
                auto saved = raw->services.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, "recall-v1");
                if (saved.result == DRAXUL_PLUGIN_STORAGE_NOT_FOUND)
                    return std::nullopt;
                if (!saved.ok()) throw std::runtime_error("Storage read failed");
                return saved.json;
            }, [raw](std::string_view value) {
                if (raw->services.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, "recall-v1", value) != DRAXUL_PLUGIN_STORAGE_OK)
                    throw std::runtime_error("Storage write failed");
            }, flashcards::unix_now, [raw] {
                return flashcards::lock_review(raw->services.path(DRAXUL_PLUGIN_PATH_CONFIG));
            });
        return instance.release();
    }
    catch (...)
    {
        instance->services.log(DRAXUL_PLUGIN_LOG_ERROR, "Flashcards could not initialize its deck, configuration or Japanese font");
        return nullptr;
    }
}
void quiesce(void* opaque) { static_cast<Instance*>(opaque)->quiesced = true; }
void destroy(void* opaque) { delete static_cast<Instance*>(opaque); }
void viewport(void* opaque, const DraxulPluginViewportV2* value)
{
    if (value && value->struct_size >= sizeof(*value))
    {
        auto& i = *static_cast<Instance*>(opaque);
        i.viewport = *value;
        i.hits.clear();
        changed(i);
    }
}
void visible(void* opaque, int32_t value)
{
    auto& i = *static_cast<Instance*>(opaque);
    i.visible = value != 0;
    if (i.visible) { i.review->refresh(); i.services.request_tick(); }
    changed(i);
}
void focused(void* opaque, int32_t value)
{
    auto& i = *static_cast<Instance*>(opaque);
    i.focused = value != 0;
    i.key_down = i.mouse_down = false;
}
int32_t input(void* opaque, const DraxulPluginInputEventV2* event)
{
    auto& i = *static_cast<Instance*>(opaque);
    if (!event || event->struct_size < sizeof(*event) || i.quiesced)
        return 0;
    if (event->kind == DRAXUL_PLUGIN_INPUT_KEY)
    {
        if (!event->pressed) { i.key_down = false; return 1; }
        if (i.key_down || !draxul::has_only_modifiers(event->modifiers & ~draxul::kModShift, draxul::kModNone)) return 1;
        i.key_down = true; // Consume held-key repeats until release.
        switch (event->logical_key)
        {
        case 32: act(i, "flip"); break;
        case 'h': case 'H': act(i, "hint"); break;
        case '1': act(i, "forgot"); break;
        case '2': act(i, "remembered"); break;
        default: return 0;
        }
        return 1;
    }
    if (event->kind == DRAXUL_PLUGIN_INPUT_POINTER_BUTTON && event->button == 1)
    {
        const float x = event->x / i.scale, y = event->y / i.scale;
        std::string action;
        for (const auto& hit : i.hits)
            if (x >= hit.x && x < hit.x + hit.w && y >= hit.y && y < hit.y + hit.h)
                action = hit.action;
        if (event->pressed && !i.mouse_down)
        {
            i.mouse_down = true;
            i.pressed_action = action;
        }
        else if (!event->pressed && i.mouse_down)
        {
            i.mouse_down = false;
            if (!action.empty() && action == i.pressed_action) act(i, action);
            i.pressed_action.clear();
        }
        return 1;
    }
    if (event->kind == DRAXUL_PLUGIN_INPUT_FOCUS && !event->pressed)
        i.key_down = i.mouse_down = false;
    return 0;
}
DraxulPluginTickResultV2 tick(void* opaque, const DraxulPluginTickInfoV2*)
{
    auto& i = *static_cast<Instance*>(opaque);
    if (!i.visible || i.quiesced) return tick_result(true, DRAXUL_PLUGIN_NO_DEADLINE);
    if (!i.review->current()) i.review->refresh();
    return tick_result(true, 30'000'000'000, true);
}
DraxulPluginRenderResultV2 vulkan(void* opaque, const DraxulPluginVulkanFrameV2* frame)
{
    auto& i = *static_cast<Instance*>(opaque);
    if (!i.visible || i.quiesced) return render_result(true);
    i.pass->set_draw_callback([&i](NVGcontext* vg, int w, int h) { draw(i, vg, w, h); });
    return render_result(i.pass->render_vulkan(*frame), "Flashcards render failed");
}
DraxulPluginRenderResultV2 metal(void* opaque, const DraxulPluginMetalFrameV2* frame)
{
    auto& i = *static_cast<Instance*>(opaque);
    if (!i.visible || i.quiesced) return render_result(true);
    i.pass->set_draw_callback([&i](NVGcontext* vg, int w, int h) { draw(i, vg, w, h); });
    return render_result(i.pass->render_metal(*frame), "Flashcards render failed");
}
int32_t presentation(void* opaque, DraxulPluginPresentationStateV2* state)
{
    auto& i = *static_cast<Instance*>(opaque);
    if (!state || state->struct_size < sizeof(*state)) return 0;
    i.status = std::to_string(i.review->due_count()) + " due";
    *state = {};
    state->struct_size = sizeof(*state);
    state->display_name = { "Flashcards", 10 };
    state->status_text = { i.status.data(), i.status.size() };
    state->background_alpha = 1;
    state->content_ready = i.quiesced ? 0 : 1;
    state->mouse_cursor = DRAXUL_PLUGIN_CURSOR_POINTER;
    return 1;
}
int32_t dispatch(void* opaque, const char* id, size_t length)
{
    if (!opaque || !id || length > 128)
        return 0;
    const auto action = std::string_view(id, length);
    if (action != "flip" && action != "hint" && action != "remembered"
        && action != "forgot" && action != "refresh")
        return 0;
    act(*static_cast<Instance*>(opaque), std::string_view(id, length));
    return 1;
}
constexpr AdapterAction actions[] = {
    { "flip", "Flip flashcard" }, { "hint", "Show image hint" },
    { "remembered", "Remembered" }, { "forgot", "Forgot" }, { "refresh", "Check due cards" }
};
using Presentation = PresentationAdapter<actions, presentation, dispatch>;
const auto api = make_plugin_api({ "dev.draxul.flashcards", "Flashcards", "0.1.0" },
    { create, quiesce, destroy, viewport, visible, focused, input, tick,
        vulkan, metal, Presentation::query_extension });
} // namespace

extern "C" DRAXUL_PLUGIN_EXPORT const DraxulPluginApiV2* draxul_plugin_query_v2(uint32_t requested)
{
    return requested == DRAXUL_PLUGIN_ABI_VERSION ? &api : nullptr;
}
