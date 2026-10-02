#pragma once
#include <algorithm>
#include <cmath>

namespace flashcards
{
// Render-independent rotation timing: both backends draw the same sampled face.
struct FlipSample
{
    float width = 1, skew = 0, shade = 0;
    bool back = false, finished = false;
};
inline FlipSample sample_flip(double elapsed, double duration = 0.58)
{
    const double t = std::clamp(elapsed / duration, 0.0, 1.0);
    const double eased = t * t * (3.0 - 2.0 * t);
    const double angle = eased * 3.141592653589793;
    return { static_cast<float>(std::max(0.012, std::abs(std::cos(angle)))),
        static_cast<float>(0.045 * std::sin(angle) * (t < 0.5 ? 1 : -1)),
        static_cast<float>(0.2 * std::sin(angle)), t >= 0.5, t >= 1 };
}
} // namespace flashcards
