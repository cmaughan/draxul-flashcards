#pragma once

#include "review.h"
#include <nanovg.h>
#include <algorithm>

namespace flashcards::cues
{
inline void box(NVGcontext* vg, float x, float y, float w, float h, NVGcolor color, float radius = 12)
{
    nvgBeginPath(vg); nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillColor(vg, color); nvgFill(vg);
}
inline void line(NVGcontext* vg, float x1, float y1, float x2, float y2, NVGcolor color, float stroke = 4)
{
    nvgBeginPath(vg); nvgMoveTo(vg, x1, y1); nvgLineTo(vg, x2, y2);
    nvgStrokeWidth(vg, stroke); nvgStrokeColor(vg, color); nvgStroke(vg);
}
inline void text(NVGcontext* vg, float x, float y, float size, const std::string& value, NVGcolor color)
{
    nvgFontFace(vg, "flashcards"); nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, color); nvgText(vg, x, y, value.c_str(), nullptr);
}
inline void person(NVGcontext* vg, float x, NVGcolor color)
{
    nvgBeginPath(vg); nvgCircle(vg, x, -47, 25); nvgFillColor(vg, color); nvgFill(vg);
    box(vg, x - 24, -15, 48, 73, color, 18);
    line(vg, x - 12, 52, x - 17, 90, color, 9);
    line(vg, x + 12, 52, x + 17, 90, color, 9);
}
inline void draw(NVGcontext* vg, const Card& card, bool back, float x, float y, float w, float h, bool preview = false)
{
    const auto ink = nvgRGB(34, 53, 73), blue = nvgRGB(63, 120, 181);
    const auto violet = nvgRGB(132, 90, 168), gold = nvgRGB(242, 184, 62);
    const float unit = std::min(w / 480, h / 240);
    nvgSave(vg); nvgTranslate(vg, x + w / 2, y + h / 2); nvgScale(vg, unit, unit);
    box(vg, -232, -113, 464, 226, nvgRGB(234, 238, 242), 18);
    if (card.cue == VisualCue::ListenerReference)
    {
        // The object shares the listener's area; the speaker points across.
        box(vg, 40, -96, 177, 197, nvgRGB(227, 212, 241), 22);
        person(vg, -156, blue); person(vg, 165, violet);
        box(vg, -221, -105, 91, 38, nvgRGB(255, 255, 255), 12);
        for (float dot : { -199.0f, -176.0f, -153.0f })
        {
            nvgBeginPath(vg); nvgCircle(vg, dot, -86, 3); nvgFillColor(vg, blue); nvgFill(vg);
        }
        line(vg, -169, -67, -158, -56, nvgRGB(255, 255, 255), 7);
        nvgBeginPath(vg); nvgArc(vg, 178, -48, 12, -1.1f, 1.1f, NVG_CW);
        nvgStrokeColor(vg, nvgRGB(255, 255, 255)); nvgStrokeWidth(vg, 3); nvgStroke(vg);
        line(vg, -143, 5, -77, 23, blue, 10);
        line(vg, -77, 23, 60, 23, ink, 3);
        line(vg, 47, 14, 60, 23, ink, 3); line(vg, 47, 32, 60, 23, ink, 3);
        box(vg, 74, 3, 51, 53, gold, 7);
        line(vg, 80, 13, 118, 13, nvgRGB(255, 231, 166), 4);
        line(vg, 74, 69, 126, 69, nvgRGB(178, 158, 131), 4);
    }
    else if (card.cue == VisualCue::SubjectMarker)
    {
        // A highlighted expression followed immediately by its attached marker.
        box(vg, -205, -37, 195, 82, gold, 12);
        if (!preview) text(vg, -107, 3, 38, card.cue_subject, ink);
        else person(vg, -107, ink);
        box(vg, -10, -37, 94, 82, nvgRGB(161, 213, 202), 10);
        if (back && !preview) text(vg, 37, 3, 38, card.kana, ink);
        box(vg, 115, -37, 97, 82, nvgRGB(216, 222, 229), 12);
        text(vg, 163, 3, 32, "…", nvgRGB(133, 150, 169));
        line(vg, -191, 61, -24, 61, nvgRGB(177, 131, 35), 4);
        line(vg, -24, 61, 37, 61, nvgRGB(54, 137, 116), 4);
        line(vg, 37, 61, 37, 49, nvgRGB(54, 137, 116), 4);
        line(vg, 21, 89, 53, 89, nvgRGB(54, 137, 116), 4);
    }
    else if (card.cue == VisualCue::ApprovalReaction)
    {
        person(vg, -163, blue); person(vg, 172, violet);
        box(vg, -116, -50, 106, 133, nvgRGB(255, 255, 255), 12);
        for (int row = 0; row < 3; ++row)
        {
            const float cy = -21 + row * 35.0f;
            line(vg, -99, cy, -91, cy + 8, nvgRGB(48, 147, 112));
            line(vg, -91, cy + 8, -77, cy - 8, nvgRGB(48, 147, 112));
            line(vg, -65, cy, -27, cy, nvgRGB(187, 201, 215), 5);
        }
        box(vg, 15, -92, 139, 101, nvgRGB(255, 255, 255), 18);
        // Thumbs-up reaction in the listener's speech bubble.
        box(vg, 49, -45, 16, 37, blue, 3);
        nvgBeginPath(vg); nvgMoveTo(vg, 68, -9); nvgLineTo(vg, 68, -42);
        nvgLineTo(vg, 79, -55); nvgLineTo(vg, 80, -74);
        nvgBezierTo(vg, 93, -76, 98, -56, 92, -47);
        nvgLineTo(vg, 119, -47); nvgBezierTo(vg, 130, -43, 121, -13, 112, -9);
        nvgClosePath(vg); nvgFillColor(vg, gold); nvgFill(vg);
        line(vg, 139, 9, 165, 20, nvgRGB(255, 255, 255), 8);
        for (float dx : { 24.0f, 63.0f, 111.0f })
        {
            line(vg, dx, 54, dx, 75, gold, 4);
            line(vg, dx - 10, 64, dx + 10, 64, gold, 4);
        }
    }
    nvgRestore(vg);
}
} // namespace flashcards::cues
