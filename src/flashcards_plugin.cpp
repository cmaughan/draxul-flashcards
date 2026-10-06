#include "review.h"
#include "review_lock.h"
#include "review_export.h"
#include "embedded_deck.h"
#include "flip.h"
#include "native_cues.h"

#include <draxul/plugin_adapter.h>
#include <draxul/plugin_host_services.h>
#include <draxul/plugin_nanovg_pass.h>
#include <draxul/input_types.h>
#include <nanovg.h>
#include <SDL3/SDL.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <set>
#include <regex>
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
    ~Instance()
    {
        pass.reset();
        if (audio_stream) SDL_DestroyAudioStream(audio_stream);
        if (audio_initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    HostServices services;
    DraxulPluginViewportV2 viewport{};
    std::unique_ptr<flashcards::ReviewSession> review;
    std::unique_ptr<flashcards::ReviewExport> review_export;
    std::filesystem::path learning_directory;
    std::string export_status, export_diagnostic;
    std::vector<unsigned char> font;
    std::map<std::string, int> images;
    std::unique_ptr<IPluginNanoVGPass> pass;
    std::vector<Hit> hits;
    float scale = 1;
    float queue_scroll = 0, queue_max_scroll = 0;
    float queue_left = 0, queue_width = 0;
    bool visible = true, focused = false, quiesced = false;
    std::set<uint32_t> held_keys;
    bool mouse_down = false;
    bool flipping = false, audio_initialized = false;
    double flip_started = 0, flip_duration = 0.58;
    SDL_AudioStream* audio_stream = nullptr;
    std::string audio_status;
    std::filesystem::path audio_directory;
    size_t audio_index = 0;
    std::filesystem::path image_directory;
    std::map<std::string, std::string> image_overrides;
    bool frame_ready_logged = false;
    std::string pressed_action;
    std::string status;
    std::vector<flashcards::Card> cue_examples;
    bool guide_visible = false, guide_acknowledged = false;
    std::string guide_error;
    size_t guide_index = 0;
};

double steady_seconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

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
std::optional<std::string> read_record(Instance& instance, std::string_view key)
{
    if (!instance.services.has_storage()) throw std::runtime_error("No persistent storage service");
    const auto saved = instance.services.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key);
    if (saved.result == DRAXUL_PLUGIN_STORAGE_NOT_FOUND) return std::nullopt;
    if (!saved.ok()) throw std::runtime_error("Storage read failed");
    return saved.json;
}
void save_record(Instance& instance, std::string_view key, std::string_view value)
{
    if (instance.services.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key, value) != DRAXUL_PLUGIN_STORAGE_OK)
        throw std::runtime_error("Storage write failed");
}
void flush_results(Instance& instance)
{
    if (!instance.review_export) return;
    std::string diagnostic;
    try
    {
        const auto guard = flashcards::lock_review(instance.services.path(DRAXUL_PLUGIN_PATH_CONFIG));
        instance.review_export->flush();
        diagnostic = instance.review_export->error();
        if (instance.review) instance.review->refresh(true);
    }
    catch (const std::exception& error) { diagnostic = error.what(); }
    const auto pending = instance.review_export->pending();
    if (diagnostic.empty())
        instance.export_status = pending == 0 ? "Shared history read locally; Dropbox may still be syncing."
            : std::to_string(pending) + " grades waiting to export; shared history read locally.";
    else if (instance.review_export->catching_up())
        instance.export_status = "Reading shared history; cached schedule retained.";
    else if (diagnostic.starts_with("Alternative"))
        instance.export_status = "Shared history read; alternative prior scores retained.";
    else
        instance.export_status = std::to_string(pending) + " grades pending; shared sync needs attention. Cached schedule retained.";
    if (diagnostic != instance.export_diagnostic && !diagnostic.empty())
        instance.services.log(DRAXUL_PLUGIN_LOG_WARNING, "Flashcards review export: " + diagnostic);
    instance.export_diagnostic = std::move(diagnostic);
}
void stop_audio(Instance& instance)
{
    if (instance.audio_stream)
    {
        SDL_DestroyAudioStream(instance.audio_stream);
        instance.audio_stream = nullptr;
    }
}
void pronounce(Instance& instance)
{
    const auto* card = instance.review->current();
    if (!instance.visible || !card || !instance.review->revealed() || instance.flipping)
        return;
    stop_audio(instance);
    instance.audio_status = "Pronunciation unavailable";
    try
    {
        if (card->audio.empty())
            throw std::runtime_error("No cached pronunciation");
        const auto& clip = card->audio.at(instance.audio_index);
        const auto bytes = read_asset((clip.private_cache ? instance.audio_directory
            : instance.services.plugin_directory() / "assets") / clip.file, 2 * 1024 * 1024);
        if (!instance.audio_initialized)
        {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) throw std::runtime_error("Audio device unavailable");
            instance.audio_initialized = true;
        }
        SDL_AudioSpec spec{};
        Uint8* samples = nullptr;
        Uint32 size = 0;
        if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true, &spec, &samples, &size))
            throw std::runtime_error("Invalid pronunciation WAV");
        std::unique_ptr<Uint8, decltype(&SDL_free)> owned(samples, SDL_free);
        if (size > 2 * 1024 * 1024 || spec.channels < 1 || spec.channels > 2 || spec.freq > 96000)
            throw std::runtime_error("Pronunciation format out of bounds");
        instance.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!instance.audio_stream || !SDL_PutAudioStreamData(instance.audio_stream, samples, static_cast<int>(size))
            || !SDL_FlushAudioStream(instance.audio_stream) || !SDL_ResumeAudioStreamDevice(instance.audio_stream))
            throw std::runtime_error("Pronunciation playback failed");
        instance.audio_status = (clip.synthetic ? "Synthetic fallback" : "Human recording")
            + std::string("  /  ") + clip.speaker
            + (card->audio.size() > 1 ? "  /  " + std::to_string(instance.audio_index + 1)
                + " of " + std::to_string(card->audio.size()) : "");
        instance.services.log(DRAXUL_PLUGIN_LOG_DEBUG, "Flashcards pronunciation queued after reveal");
        instance.services.log(DRAXUL_PLUGIN_LOG_DEBUG, "Flashcards pronunciation clip="
            + std::to_string(instance.audio_index) + " synthetic=" + std::to_string(clip.synthetic));
    }
    catch (...)
    {
        stop_audio(instance);
        instance.services.log(DRAXUL_PLUGIN_LOG_WARNING, "Flashcards cached pronunciation unavailable");
    }
}
void act(Instance& instance, std::string_view action)
{
    if (instance.quiesced || !instance.visible)
        return;
    if (action == "queue-up" || action == "queue-down")
    {
        instance.queue_scroll = std::clamp(instance.queue_scroll
            + (action == "queue-up" ? -260.0f : 260.0f), 0.0f, instance.queue_max_scroll);
        changed(instance); return;
    }
    if (action == "help")
    {
        stop_audio(instance); instance.flipping = false;
        instance.guide_visible = true; instance.guide_index = 0; changed(instance); return;
    }
    if (instance.guide_visible)
    {
        if (action == "close-guide")
        {
            if (instance.guide_index + 1 < instance.cue_examples.size())
            {
                ++instance.guide_index; changed(instance); return;
            }
            if (!instance.guide_acknowledged
                && instance.services.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, "cue-guide-v1",
                    R"({"schema_version":1,"acknowledged":true})") != DRAXUL_PLUGIN_STORAGE_OK)
                instance.guide_error = "The guide preference could not be saved. Please retry.";
            else
            {
                instance.guide_acknowledged = true; instance.guide_visible = false;
                instance.guide_error.clear(); instance.services.request_tick();
            }
            changed(instance);
        }
        return; // Studying the guide never plays answers or records grades.
    }
    instance.services.log(DRAXUL_PLUGIN_LOG_DEBUG, "Flashcards action " + std::string(action)
        + " revealed=" + std::to_string(instance.review->revealed())
        + " image=" + std::to_string(instance.review->image_ready()));
    if (instance.flipping) return;
    if (action == "flip" && instance.review->current() && !instance.review->revealed())
    {
        instance.audio_index = 0;
        instance.flip_started = steady_seconds();
        instance.flipping = true;
        instance.services.request_tick();
    }
    else if (action == "replay") pronounce(instance);
    else if (action == "another-speaker" && instance.review->revealed() && instance.review->current())
    {
        const auto count = instance.review->current()->audio.size();
        if (count > 1)
        {
            instance.audio_index = flashcards::next_audio_index(instance.audio_index, count);
            pronounce(instance);
        }
    }
    else if (action == "remembered" || action == "again")
    {
        flush_results(instance); // Import arriving peer grades before the locked stale-grade check.
        if (instance.review->grade(action == "remembered") || !instance.review->revealed())
        {
            instance.queue_scroll = 0;
            stop_audio(instance);
            instance.audio_status.clear();
            instance.audio_index = 0;
        }
        flush_results(instance);
        if (!instance.review->error().empty())
            instance.services.log(DRAXUL_PLUGIN_LOG_WARNING, instance.review->error());
    }
    else if (action == "skip" || action == "refresh")
    {
        stop_audio(instance);
        instance.audio_status.clear();
        instance.audio_index = 0;
        if (action == "skip") instance.review->skip();
        else instance.review->start_batch();
        instance.queue_scroll = 0;
    }
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
    nvgFontFace(vg, "flashcards");
    nvgFontSize(vg, 17);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGB(243, 246, 255));
    nvgText(vg, x + w / 2, y + 22, text.c_str(), nullptr);
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
        auto path = instance.image_directory / name;
        if (instance.image_directory.empty() || !std::filesystem::exists(path))
            path = instance.services.plugin_directory() / "assets" / name;
        const auto bytes = read_asset(path, 8 * 1024 * 1024);
        int w = 0, h = 0, components = 0;
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &components)
            || w <= 0 || h <= 0 || w > 4096 || h > 4096)
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
void picture(NVGcontext* vg, int handle, float x, float y, float w, float h)
{
    int image_w = 0, image_h = 0;
    nvgImageSize(vg, handle, &image_w, &image_h);
    if (image_w <= 0 || image_h <= 0) return;
    const float ratio = std::min(w / image_w, h / image_h);
    const float shown_w = image_w * ratio, shown_h = image_h * ratio;
    x += (w - shown_w) / 2; y += (h - shown_h) / 2;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, shown_w, shown_h, 12);
    nvgFillPaint(vg, nvgImagePattern(vg, x, y, shown_w, shown_h, 0, handle, 1));
    nvgFill(vg);
}
void printed_word_scene(NVGcontext* vg, int handle, float x, float y, float w, float h)
{
    // Keep an actual printed word sharp; never draw the card's answer into the cue.
    // Source pixels stay unchanged. This photographic display adaptation is CC BY-SA 4.0.
    const float unit = std::min(w / 600, h / 320);
    nvgSave(vg); nvgTranslate(vg, x + w / 2, y + h / 2); nvgScale(vg, unit, unit);
    rect(vg, -280, -140, 560, 280, nvgRGB(239, 230, 209), 16);
    nvgIntersectScissor(vg, -280, -140, 560, 280);
    nvgSave(vg);
    nvgRotate(vg, -0.85f);
    constexpr float scale = 0.92f;
    const float px = -1275 * scale, py = -3060 * scale;
    nvgBeginPath(vg); nvgRect(vg, px, py, 2448 * scale, 3264 * scale);
    nvgFillPaint(vg, nvgImagePattern(vg, px, py, 2448 * scale, 3264 * scale, 0, handle, 1));
    nvgFill(vg); nvgRestore(vg);
    nvgBeginPath(vg); nvgRect(vg, -280, -140, 560, 280);
    nvgRoundedRect(vg, -101, -48, 202, 96, 9); nvgPathWinding(vg, NVG_HOLE);
    nvgFillColor(vg, nvgRGBA(247, 242, 228, 205)); nvgFill(vg);
    nvgBeginPath(vg); nvgRoundedRect(vg, -101, -48, 202, 96, 9);
    nvgFillColor(vg, nvgRGBA(255, 211, 89, 25)); nvgFill(vg);
    nvgStrokeColor(vg, nvgRGB(193, 133, 26)); nvgStrokeWidth(vg, 3); nvgStroke(vg);
    nvgRestore(vg);
}
void morning_scene(NVGcontext* vg, int handle, float x, float y, float w, float h)
{
    // Waking in bed, daylight and an early alarm supply a specific morning cue.
    const float unit = std::min(w / 420, h / 250);
    nvgSave(vg); nvgTranslate(vg, x + w / 2, y + h / 2); nvgScale(vg, unit, unit);
    rect(vg, -204, -119, 408, 238, nvgRGB(241, 231, 207), 16);
    picture(vg, handle, -189, -109, 145, 218);
    rect(vg, -19, -99, 200, 124, nvgRGB(255, 255, 255), 8);
    rect(vg, -11, -91, 184, 108, nvgRGB(179, 224, 247), 4);
    nvgBeginPath(vg); nvgCircle(vg, 38, -57, 23);
    nvgFillColor(vg, nvgRGB(255, 215, 94)); nvgFill(vg);
    rect(vg, 78, -91, 5, 108, nvgRGB(255, 255, 255), 0);
    rect(vg, -11, -39, 184, 5, nvgRGB(255, 255, 255), 0);
    rect(vg, 14, 51, 146, 58, nvgRGB(42, 61, 79), 12);
    nvgFontFace(vg, "flashcards"); nvgFontSize(vg, 32);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGB(244, 249, 239));
    nvgText(vg, 87, 80, "07:00", nullptr);
    for (float sign : { -1.0f, 1.0f })
    {
        nvgBeginPath(vg); nvgMoveTo(vg, 87 + sign * 82, 69);
        nvgLineTo(vg, 87 + sign * 91, 61); nvgLineTo(vg, 87 + sign * 88, 49);
        nvgStrokeColor(vg, nvgRGB(171, 109, 62)); nvgStrokeWidth(vg, 4); nvgStroke(vg);
    }
    nvgRestore(vg);
}
void software_scene(NVGcontext* vg, int handle, float x, float y, float w, float h)
{
    // A software helper completing tasks: no target spelling or text on the cue.
    const float cx = x + w / 2, cy = y + h / 2;
    const float unit = std::min(w / 480, h / 260);
    nvgSave(vg); nvgTranslate(vg, cx, cy); nvgScale(vg, unit, unit);
    rect(vg, -216, -104, 432, 208, nvgRGB(228, 237, 246), 22);
    rect(vg, -102, -74, 204, 148, nvgRGB(34, 55, 79), 12);
    rect(vg, -90, -64, 180, 122, nvgRGB(248, 251, 255), 8);
    picture(vg, handle, -69, -58, 138, 116);
    rect(vg, -123, 76, 246, 12, nvgRGB(81, 101, 126), 6);
    for (int i = 0; i < 3; ++i)
    {
        const float ty = -76 + i * 58.0f;
        rect(vg, 120, ty, 77, 44, nvgRGB(255, 255, 255), 9);
        nvgBeginPath(vg); nvgMoveTo(vg, 132, ty + 21); nvgLineTo(vg, 140, ty + 29); nvgLineTo(vg, 153, ty + 13);
        nvgStrokeColor(vg, nvgRGB(28, 139, 117)); nvgStrokeWidth(vg, 3); nvgStroke(vg);
        rect(vg, 160, ty + 16, 24, 4, nvgRGB(148, 168, 190), 2);
        rect(vg, 160, ty + 25, 17, 4, nvgRGB(181, 194, 210), 2);
    }
    nvgBeginPath(vg); nvgMoveTo(vg, -182, -15); nvgLineTo(vg, -130, -15);
    nvgMoveTo(vg, -140, -24); nvgLineTo(vg, -130, -15); nvgLineTo(vg, -140, -6);
    nvgStrokeColor(vg, nvgRGB(91, 132, 180)); nvgStrokeWidth(vg, 3); nvgStroke(vg);
    rect(vg, -197, -63, 52, 32, nvgRGB(255, 255, 255), 8);
    nvgBeginPath(vg); nvgCircle(vg, -185, -49, 3); nvgCircle(vg, -171, -49, 3); nvgCircle(vg, -157, -49, 3);
    nvgFillColor(vg, nvgRGB(91, 132, 180)); nvgFill(vg);
    nvgRestore(vg);
}
// Both production faces and queue images use the same cue. Preview mode has no
// subject spelling and never draws answer metadata or a revealing tooltip.
bool draw_cue(Instance& instance, NVGcontext* vg, const flashcards::Card& card,
    bool back, float x, float y, float w, float h, bool preview = false)
{
    const bool custom = instance.image_overrides.contains(card.id);
    const std::string name = custom ? instance.image_overrides.at(card.id) : card.image;
    if (!custom && card.cue != flashcards::VisualCue::Picture)
    {
        flashcards::cues::draw(vg, card, back, x, y, w, h, preview);
        return true;
    }
    const int handle = image(instance, vg, name);
    if (handle <= 0) return false;
    if (!custom && name == "assistant-crisp.png") software_scene(vg, handle, x, y, w, h);
    else if (!custom && name == "morning-wakeup.jpg") morning_scene(vg, handle, x, y, w, h);
    else if (!custom && name == "printed-word.png") printed_word_scene(vg, handle, x, y, w, h);
    else if (!custom && name == "photo-cue.png")
    {
        const float unit = std::min(w / 480, h / 260);
        nvgSave(vg); nvgTranslate(vg, x + w / 2, y + h / 2); nvgScale(vg, unit, unit);
        rect(vg, -225, -115, 450, 230, nvgRGB(230, 236, 242), 15);
        const int camera = image(instance, vg, "camera-cue.png");
        if (camera > 0) picture(vg, camera, -208, -93, 185, 185);
        picture(vg, handle, 38, -96, 185, 185);
        flashcards::cues::line(vg, -13, 0, 30, 0, nvgRGB(63, 120, 181), 5);
        flashcards::cues::line(vg, 19, -11, 30, 0, nvgRGB(63, 120, 181), 5);
        flashcards::cues::line(vg, 19, 11, 30, 0, nvgRGB(63, 120, 181), 5);
        nvgRestore(vg);
        return camera > 0;
    }
    else
    {
        picture(vg, handle, x, y, w, h);
        if (!custom && name == "today-cue.png")
        {
            // Highlight the present day on a wordless calendar. Original pixels
            // stay unchanged; this display adaptation shares CC BY-SA 4.0.
            const float unit = std::min(w, h) / 618.0f;
            const float cx = x + w / 2 - 51 * unit, cy = y + h / 2 + 31 * unit;
            nvgBeginPath(vg); nvgCircle(vg, cx, cy, 35 * unit);
            nvgStrokeColor(vg, nvgRGB(222, 153, 35)); nvgStrokeWidth(vg, 10 * unit); nvgStroke(vg);
        }
    }
    return true;
}

void draw_queue(Instance& instance, NVGcontext* vg, float x, float w)
{
    instance.queue_left = x; instance.queue_width = w;
    const auto queue = instance.review->queue();
    constexpr float top = 132, clip_height = 466, row_height = 108;
    const auto muted = nvgRGB(153, 173, 197), accent = nvgRGB(132, 211, 192);
    rect(vg, x, 96, w, 502, nvgRGB(25, 40, 58), 16);
    label(vg, x + 12, 107, 15, "Up next", nvgRGB(237, 243, 250));
    float content_height = 0;
    std::string previous;
    size_t due_index = 0;
    for (const auto& entry : queue)
    {
        const std::string group = entry.due_now
            ? (due_index++ < instance.review->remaining_grades() ? "Due" : "Next batch") : "Later";
        if (group != previous) { content_height += 25; previous = group; }
        content_height += row_height;
    }
    instance.queue_max_scroll = std::max(0.0f, content_height - clip_height);
    instance.queue_scroll = std::clamp(instance.queue_scroll, 0.0f, instance.queue_max_scroll);
    nvgSave(vg); nvgIntersectScissor(vg, x, top, w, clip_height);
    float y = top - instance.queue_scroll;
    previous.clear(); due_index = 0;
    for (size_t index = 0; index < queue.size(); ++index)
    {
        const auto& entry = queue[index];
        const std::string group = entry.due_now
            ? (due_index++ < instance.review->remaining_grades() ? "Due" : "Next batch") : "Later";
        if (group != previous)
        {
            label(vg, x + 12, y + 3, 11, group, muted);
            previous = group; y += 25;
        }
        if (y + row_height >= top && y < top + clip_height)
        {
            rect(vg, x + 8, y, w - 20, 100, entry.current ? nvgRGB(53, 83, 104) : nvgRGB(36, 53, 70), 10);
            rect(vg, x + 13, y + 5, w - 30, 70, nvgRGB(248, 247, 242), 7);
            if (!draw_cue(instance, vg, *entry.card, false, x + 17, y + 8, w - 38, 64, true))
                label(vg, x + w / 2 - 3, y + 30, 11, "Image unavailable", muted, NVG_ALIGN_CENTER);
            label(vg, x + 14, y + 80, 10, entry.current ? "Now"
                : "#" + std::to_string(index + 1), accent);
            label(vg, x + w - 18, y + 80, 10,
                entry.card->direction == flashcards::Direction::Production ? "Say" : "Read", muted, NVG_ALIGN_RIGHT);
        }
        y += row_height;
    }
    if (queue.empty()) label(vg, x + 12, top + 15, 11, "No queue available", muted);
    nvgRestore(vg);
    if (instance.queue_max_scroll > 0)
    {
        const float thumb = std::max(24.0f, clip_height * clip_height / content_height);
        const float sy = top + (clip_height - thumb) * instance.queue_scroll / instance.queue_max_scroll;
        rect(vg, x + w - 7, sy, 3, thumb, nvgRGB(100, 126, 148), 2);
        button(instance, vg, x, 608, (w - 8) / 2, "Up", "queue-up", nvgRGB(34, 55, 77));
        button(instance, vg, x + (w + 8) / 2, 608, (w - 8) / 2, "Down", "queue-down", nvgRGB(34, 55, 77));
    }
}

void draw_guide(Instance& instance, NVGcontext* vg, float left, float panel, float width)
{
    const auto ink = nvgRGB(34, 53, 73), muted = nvgRGB(80, 100, 122);
    rect(vg, left, 96, panel, 422, nvgRGB(248, 247, 242), 22);
    label(vg, left + 24, 110, 23, "How to read these cues", ink);
    if (!instance.cue_examples.empty())
    {
        const auto& example = instance.cue_examples.at(instance.guide_index);
        const float x = left + 24, column = panel - 48;
        flashcards::cues::draw(vg, example, true, x, 150, column, 160);
        const bool reference = example.cue == flashcards::VisualCue::ListenerReference;
        const bool approval = example.cue == flashcards::VisualCue::ApprovalReaction;
        label(vg, x, 326, 21, reference ? "それ  /  That near the listener" : approval ? "いいね  /  That's great!" : "が  /  Subject marker", ink);
        paragraph(vg, x, 365, column, 16, reference
            ? "Blue speaks; purple listens. The speaker points to the gold object beside the listener. Recall that object as seen from the speaker. This card teaches the near-listener use."
            : approval ? "One person shows completed work; the other responds with a friendly thumbs-up. Recall the whole casual approval phrase, いいね, as one item. It is a friendly 'That's great!' or 'Nice!'."
            : "Gold highlights the subject, それ. The attached green chip immediately AFTER it holds が. On a production front, that chip is blank: recall the taught subject marker. が does not mean 'is'.", muted);
        if (!reference && !approval)
            paragraph(vg, x, 466, column, 12,
                "This is a fragment, not a complete sentence. Other particle choices and uses need context and are outside this card.", muted);
    }
    else paragraph(vg, left + 28, 166, panel - 56, 19,
        "Recognition starts with kana; production starts with a visual cue. Reveal when ready, then choose Again or Remembered. Pronunciation plays only after revealing; R replays it.", muted);
    const bool more = instance.guide_index + 1 < instance.cue_examples.size();
    button(instance, vg, left, 542, panel, more ? "Next cue" : "Continue to reviews", "close-guide", nvgRGB(57, 88, 143));
    label(vg, width / 2, 602, 12, "English help: H or Help. Space continues. "
        + std::to_string(instance.guide_index + 1) + "/" + std::to_string(std::max<size_t>(1, instance.cue_examples.size())), nvgRGB(153, 173, 197), NVG_ALIGN_CENTER);
    if (!instance.guide_error.empty())
        paragraph(vg, left, 630, panel, 12, instance.guide_error, nvgRGB(255, 197, 121));
}
void draw(Instance& instance, NVGcontext* vg, int pixel_w, int pixel_h)
{
    instance.hits.clear();
    if (nvgFindFont(vg, "flashcards") < 0)
    {
        instance.images.clear();
        if (nvgCreateFontMem(vg, "flashcards", instance.font.data(), static_cast<int>(instance.font.size()), 0) < 0)
        {
            instance.services.log(DRAXUL_PLUGIN_LOG_ERROR, "Flashcards Japanese font failed to load");
            return;
        }
    }
    instance.scale = std::max(0.1f, std::min(pixel_w / 800.0f, pixel_h / 660.0f));
    nvgSave(vg); nvgScale(vg, instance.scale, instance.scale);
    const float width = pixel_w / instance.scale, height = pixel_h / instance.scale;
    const float queue_width = std::min(200.0f, width * 0.22f);
    const float panel = std::min(width - queue_width - 84, 740.0f);
    const float group_left = (width - panel - queue_width - 20) / 2;
    const float left = group_left + queue_width + 20, center = left + panel / 2;
    const auto white = nvgRGB(237, 243, 250), muted = nvgRGB(153, 173, 197), ink = nvgRGB(27, 45, 66);
    rect(vg, 0, 0, width, height, nvgRGB(16, 27, 43), 0);
    label(vg, left, 21, 24, "Japanese flashcards", white);
    label(vg, left, 59, 13, std::to_string(instance.review->due_count()) + " reviews due", muted);
    if (instance.guide_visible)
    {
        draw_guide(instance, vg, left, panel, center * 2);
        nvgRestore(vg);
        if (!instance.frame_ready_logged)
        {
            instance.services.log(DRAXUL_PLUGIN_LOG_DEBUG, "Flashcards frame ready");
            instance.frame_ready_logged = true;
        }
        return;
    }
    draw_queue(instance, vg, group_left, queue_width);
    button(instance, vg, left + panel - 94, 48, 94, "Help", "help", nvgRGB(34, 55, 77));
    const auto* card = instance.review->current();
    if (card)
    {
        const bool production = card->direction == flashcards::Direction::Production;
        label(vg, left + panel, 28, 12, production ? "PRODUCTION" : "RECOGNITION", nvgRGB(132, 211, 192), NVG_ALIGN_RIGHT);
        std::string image_name = card->image;
        const bool custom = instance.image_overrides.contains(card->id);
        if (custom) image_name = instance.image_overrides.at(card->id);
        const bool native = !custom && card->cue != flashcards::VisualCue::Picture;
        const int handle = native ? 0 : image(instance, vg, image_name);
        const bool visual_ready = native || handle > 0;
        instance.review->set_image_ready(visual_ready);
        auto frame = instance.flipping ? flashcards::sample_flip(steady_seconds() - instance.flip_started, instance.flip_duration)
            : flashcards::FlipSample{ 1, 0, 0, instance.review->revealed(), true };
        const float cy = 307;
        // Ground shadow and a shaded edge keep the turn legible at its midpoint.
        nvgBeginPath(vg); nvgEllipse(vg, center, 527, panel * 0.46f * frame.width, 10);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 70)); nvgFill(vg);
        nvgSave(vg); nvgTranslate(vg, center, cy);
        nvgTransform(vg, frame.width, frame.skew, 0, 1, 0, 0);
        nvgTranslate(vg, -center, -cy);
        rect(vg, left + 3, 99, panel, 422, nvgRGB(152, 165, 177), 22);
        rect(vg, left, 96, panel, 422, nvgRGB(248, 247, 242), 22);
        if (frame.width > 0.055f)
        {
            if (!frame.back && !production)
            {
                const float size = std::min(76.0f, (panel - 64) / std::max<size_t>(1, card->kana.size() / 3));
                label(vg, center, cy - size * 0.6f, size, card->kana, ink, NVG_ALIGN_CENTER);
            }
            else
            {
                const float image_y = frame.back ? 116 : 126;
                const float image_h = frame.back ? 168 : 350;
                if (visual_ready)
                {
                    draw_cue(instance, vg, *card, frame.back, left + 24, image_y, panel - 48, image_h);
                }
                else paragraph(vg, left + 36, 248, panel - 72, 19, "Picture unavailable. Add a personal image or skip this review.", nvgRGB(91, 110, 130));
                if (frame.back)
                {
                    const float size = std::min(43.0f, (panel - 64) / std::max<size_t>(1, card->kana.size() / 3));
                    label(vg, center, 292, size, card->kana, ink, NVG_ALIGN_CENTER);
                    label(vg, center, 342, 16, card->romaji, nvgRGB(96, 114, 132), NVG_ALIGN_CENTER);
                    paragraph(vg, left + 24, 369, panel - 48, 15, card->meaning, ink);
                    if (!instance.flipping && !card->audio.empty())
                    {
                        const bool multiple = card->audio.size() > 1;
                        const float audio_width = multiple ? (panel - 64) / 2 : std::min(240.0f, panel - 48);
                        button(instance, vg, multiple ? left + 24 : center - audio_width / 2, 449, audio_width,
                            "R  Replay", "replay", nvgRGB(52, 86, 119));
                        if (multiple) button(instance, vg, center + 8, 449, audio_width,
                            "N  Another speaker", "another-speaker", nvgRGB(52, 86, 119));
                    }
                    if (!instance.flipping)
                        label(vg, center, 496, 11, instance.audio_status, nvgRGB(99, 116, 134), NVG_ALIGN_CENTER);
                }
            }
        }
        if (frame.shade > 0)
            rect(vg, left, 96, panel, 422, nvgRGBA(15, 30, 50, static_cast<unsigned char>(frame.shade * 255)), 22);
        nvgRestore(vg);
        if (instance.flipping)
            label(vg, center, 557, 15, "Turning card...", muted, NVG_ALIGN_CENTER);
        else if (instance.review->revealed() && visual_ready)
        {
            const float half = (panel - 16) / 2;
            button(instance, vg, left, 542, half, "1  Again", "again", nvgRGB(132, 74, 78));
            button(instance, vg, left + half + 16, 542, half, "2  Remembered", "remembered", nvgRGB(28, 118, 103));
        }
        else if ((instance.review->revealed() || production) && !visual_ready)
            button(instance, vg, left, 542, panel, "Skip this review", "skip", nvgRGB(59, 80, 108));
        else
        {
            instance.hits.push_back({ left, 96, panel, 422, "flip" });
            button(instance, vg, left, 542, panel, "Space  Reveal", "flip", nvgRGB(57, 88, 143));
        }
        if (instance.review->revealed() && !instance.flipping)
        {
            const auto credit = custom ? std::string("Personal picture") : card->attribution;
            const auto audio_credit = card->audio.empty() ? std::string()
                : "\n" + card->audio.at(instance.audio_index).attribution;
            paragraph(vg, left, 598, panel, 10, credit + audio_credit, muted);
        }
        else label(vg, center, 603, 13, production ? "Recall the Japanese word. Reveal when ready." : "Recall the meaning. Reveal when ready.", muted, NVG_ALIGN_CENTER);
    }
    else
    {
        rect(vg, left, 96, panel, 422, nvgRGB(29, 45, 65), 22);
        label(vg, center, 236, 30, instance.review->blocked() ? "Review unavailable"
            : instance.review->deck_size() == 0 ? "Your deck is empty"
            : instance.review->batch_finished() ? "Session complete"
            : instance.review->due_count() > 0 ? "Pictures needed" : "All caught up", white, NVG_ALIGN_CENTER);
        std::string next = "Take a break. New words arrive with your next build.";
        if (const auto due = instance.review->next_due())
            next = "Next review in about " + std::to_string(std::max<int64_t>(1, (*due - flashcards::unix_now() + 59) / 60)) + " minutes.";
        paragraph(vg, left + 40, 310, panel - 80, 18, next, muted);
        button(instance, vg, left, 542, panel, "Check due reviews", "refresh", nvgRGB(57, 88, 143));
    }
    if (!instance.review->error().empty())
        paragraph(vg, left, height - 40, panel, 10, instance.review->error(), nvgRGB(255, 197, 121));
    if (!instance.export_status.empty())
        paragraph(vg, left, height - 14, panel, 10, instance.export_status, nvgRGB(153, 173, 197));
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
        if (!config || !config->is_object() || config->size() > 5)
            throw std::runtime_error("Invalid Flashcards configuration");
        for (const auto& [name, value] : config->items())
        {
            if (name == "image_directory")
            {
                const auto path = value.get<std::string>();
                if (path.size() > 4096 || !std::filesystem::path(path).is_absolute())
                    throw std::runtime_error("Picture directory must be absolute");
                instance->image_directory = std::filesystem::u8path(path);
            }
            else if (name == "images")
            {
                if (!value.is_object() || value.size() > 2000) throw std::runtime_error("Invalid picture overrides");
                for (const auto& [id, image] : value.items())
                {
                    const auto file = image.get<std::string>();
                    if (id.size() > 96 || !std::regex_match(file, std::regex("[a-z0-9_-]+\\.(png|jpg|jpeg)")))
                        throw std::runtime_error("Invalid picture override");
                    instance->image_overrides.emplace(id, file);
                }
            }
            else if (name == "audio_directory")
            {
                if (!value.is_string()) throw std::runtime_error("Invalid audio directory");
                instance->audio_directory = std::filesystem::path(value.get<std::string>());
                if (!instance->audio_directory.is_absolute()) throw std::runtime_error("Audio directory must be absolute");
                instance->audio_directory = std::filesystem::weakly_canonical(instance->audio_directory);
                const auto plugin = std::filesystem::weakly_canonical(instance->services.plugin_directory());
                const auto relative = instance->audio_directory.lexically_relative(plugin);
                if (!relative.empty() && *relative.begin() != "..")
                    throw std::runtime_error("Private audio must be outside the package directory");
            }
            else if (name == "flip_duration_ms")
            {
                if (!value.is_number_integer()) throw std::runtime_error("Invalid flip duration");
                const auto milliseconds = value.get<int>();
                if (milliseconds < 100 || milliseconds > 3000) throw std::runtime_error("Flip duration outside bounds");
                instance->flip_duration = milliseconds / 1000.0;
            }
            else if (name == "learning_directory")
            {
                const auto directory = value.get<std::string>();
                if (directory.size() > 4096 || !std::filesystem::u8path(directory).is_absolute())
                    throw std::runtime_error("Shared learning directory must be absolute");
                instance->learning_directory = std::filesystem::u8path(directory);
            }
            else throw std::runtime_error("Unknown Flashcards configuration");
        }
        instance->viewport = info->initial_viewport;
        instance->font = read_asset(instance->services.plugin_directory() / "assets/NotoSansJP.otf", 16 * 1024 * 1024);
        instance->pass = create_plugin_nanovg_pass({ instance->services.plugin_directory() });
        auto* raw = instance.get();
        const auto settings = read_record(*raw, "learning-settings-v1");
        if (instance->learning_directory.empty() && settings)
        {
            const auto saved = nlohmann::json::parse(*settings);
            if (!saved.is_object() || saved.size() != 2 || !saved.at("schema_version").is_number_integer()
                || saved.at("schema_version") != 1 || !saved.at("directory").is_string())
                throw std::runtime_error("Invalid shared learning settings");
            const auto directory = saved.at("directory").get<std::string>();
            if (directory.size() > 4096 || !std::filesystem::u8path(directory).is_absolute())
                throw std::runtime_error("Shared learning directory must be absolute");
            instance->learning_directory = std::filesystem::u8path(directory);
        }
        if (!instance->learning_directory.empty())
        {
            instance->learning_directory = std::filesystem::weakly_canonical(instance->learning_directory);
            const auto relative = instance->learning_directory.lexically_relative(
                std::filesystem::weakly_canonical(instance->services.plugin_directory()));
            if (!relative.empty() && *relative.begin() != "..")
                throw std::runtime_error("Shared learning data must be outside the package directory");
            if (config->contains("learning_directory"))
                save_record(*raw, "learning-settings-v1", nlohmann::json({{"schema_version", 1},
                    {"directory", config->at("learning_directory")}}).dump());
            instance->review_export = std::make_unique<flashcards::ReviewExport>(instance->learning_directory,
                instance->services.path(DRAXUL_PLUGIN_PATH_CONFIG) / "review-sync-v1",
                [raw] { return read_record(*raw, "review-exports-v1"); },
                [raw](std::string_view value) { save_record(*raw, "review-exports-v1", value); },
                [raw] { return read_record(*raw, "recall-v1"); },
                [raw](std::string_view value) { save_record(*raw, "recall-v1", value); });
            flush_results(*raw);
        }
        auto cards = flashcards::parse_deck(flashcards::embedded::deck);
        if (!instance->audio_directory.empty())
        {
            const auto bytes = read_asset(instance->audio_directory / "manifest.json", 1024 * 1024);
            const auto overrides = flashcards::parse_private_audio(
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), cards);
            for (auto& card : cards)
                if (const auto found = overrides.find(card.id); found != overrides.end())
                    card.audio = found->second; // Explicit private selection never silently falls back.
        }
        std::set<flashcards::VisualCue> examples;
        for (const auto& card : cards)
            if (card.cue != flashcards::VisualCue::Picture && examples.insert(card.cue).second)
                instance->cue_examples.push_back(card);
        if (!instance->cue_examples.empty())
        {
            const auto saved = instance->services.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, "cue-guide-v1");
            if (saved.ok())
            {
                const auto doc = nlohmann::json::parse(saved.json, nullptr, false);
                instance->guide_acknowledged = doc.is_object() && doc.size() == 2
                    && doc.contains("schema_version") && doc["schema_version"].is_number_integer() && doc["schema_version"] == 1
                    && doc.contains("acknowledged") && doc["acknowledged"].is_boolean() && doc["acknowledged"] == true;
            }
            instance->guide_visible = !instance->guide_acknowledged;
        }
        instance->review = std::make_unique<flashcards::ReviewSession>(std::move(cards),
            [raw] { return read_record(*raw, "recall-v1"); }, [raw](std::string_view value) {
                if (raw->review_export) raw->review_export->commit(value);
                else save_record(*raw, "recall-v1", value);
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
void quiesce(void* opaque)
{
    auto& i = *static_cast<Instance*>(opaque);
    i.quiesced = true;
    stop_audio(i);
}
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
    if (!i.visible) { stop_audio(i); i.flipping = false; }
    if (i.visible) { i.review->refresh(); i.services.request_tick(); }
    changed(i);
}
void focused(void* opaque, int32_t value)
{
    auto& i = *static_cast<Instance*>(opaque);
    i.focused = value != 0;
    i.held_keys.clear(); i.mouse_down = false;
}
int32_t input(void* opaque, const DraxulPluginInputEventV2* event)
{
    auto& i = *static_cast<Instance*>(opaque);
    if (!event || event->struct_size < sizeof(*event) || i.quiesced)
        return 0;
    if (event->kind == DRAXUL_PLUGIN_INPUT_KEY)
    {
        if (!event->pressed) { i.held_keys.erase(event->logical_key); return 1; }
        if (!i.held_keys.insert(event->logical_key).second
            || !draxul::has_only_modifiers(event->modifiers & ~draxul::kModShift, draxul::kModNone)) return 1;
        if (i.guide_visible)
        {
            if (event->logical_key == 32 || event->logical_key == 13 || event->logical_key == 27)
                act(i, "close-guide");
            return 1;
        }
        switch (event->logical_key)
        {
        case 32: act(i, "flip"); break;
        case 'r': case 'R': act(i, "replay"); break;
        case 'n': case 'N': act(i, "another-speaker"); break;
        case 'h': case 'H': case '?': act(i, "help"); break;
        case '1': act(i, "again"); break;
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
    if (event->kind == DRAXUL_PLUGIN_INPUT_WHEEL && !i.guide_visible)
    {
        const float x = event->x / i.scale;
        if (x >= i.queue_left && x < i.queue_left + i.queue_width)
        {
            i.queue_scroll = std::clamp(i.queue_scroll - event->delta_y * 72.0f,
                0.0f, i.queue_max_scroll);
            changed(i); return 1;
        }
    }
    if (event->kind == DRAXUL_PLUGIN_INPUT_FOCUS && !event->pressed)
    {
        i.held_keys.clear(); i.mouse_down = false;
    }
    return 0;
}
DraxulPluginTickResultV2 tick(void* opaque, const DraxulPluginTickInfoV2*)
{
    auto& i = *static_cast<Instance*>(opaque);
    if (!i.visible || i.quiesced) return tick_result(true, DRAXUL_PLUGIN_NO_DEADLINE);
    if (!i.flipping) flush_results(i);
    const auto retry_delay = i.review_export && i.review_export->catching_up() ? 1'000'000'000 : 30'000'000'000;
    if (i.guide_visible) return tick_result(true, i.review_export ? retry_delay : DRAXUL_PLUGIN_NO_DEADLINE);
    if (i.flipping)
    {
        if (flashcards::sample_flip(steady_seconds() - i.flip_started, i.flip_duration).finished)
        {
            i.flipping = false;
            if (i.review->flip())
            {
                i.services.log(DRAXUL_PLUGIN_LOG_DEBUG, "Flashcards reveal complete");
                pronounce(i);
            }
            changed(i);
        }
        else return tick_result(true, 16'666'667, true);
    }
    if (!i.review->current()) i.review->refresh();
    return tick_result(true, retry_delay, true);
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
    if (action != "flip" && action != "replay" && action != "remembered"
        && action != "again" && action != "refresh" && action != "skip"
        && action != "help" && action != "close-guide" && action != "another-speaker")
        return 0;
    act(*static_cast<Instance*>(opaque), std::string_view(id, length));
    return 1;
}
constexpr AdapterAction actions[] = {
    { "flip", "Reveal flashcard" }, { "replay", "Replay pronunciation" },
    { "remembered", "Remembered" }, { "again", "Again" }, { "skip", "Skip flashcard" },
    { "refresh", "Check due reviews" }, { "help", "Explain card cues" }, { "close-guide", "Continue to reviews" },
    { "another-speaker", "Hear another speaker" }
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
