#include "Modulators.h"

namespace be
{
std::shared_ptr<ShapeLut> ShapeLut::fromPoints (const std::vector<ShapePoint>& ptsIn)
{
    auto lut = std::make_shared<ShapeLut>();
    auto pts = ptsIn;
    if (pts.size() < 2) pts = defaultLfoShape();
    std::sort (pts.begin(), pts.end(), [] (const ShapePoint& a, const ShapePoint& b) { return a.x < b.x; });
    for (int i = 0; i <= N; ++i)
    {
        const float x = (float) i / (float) N;
        size_t k = 0;
        while (k + 1 < pts.size() && pts[k + 1].x < x) ++k;
        if (k + 1 >= pts.size()) { lut->v[i] = pts.back().y; continue; }
        const auto& a = pts[k];
        const auto& b = pts[k + 1];
        if (x <= a.x) { lut->v[i] = a.y; continue; }
        const float span = std::max (1.0e-5f, b.x - a.x);
        const float t = curveFn ((x - a.x) / span, b.curve);
        lut->v[i] = clamp01 (a.y + (b.y - a.y) * t);
    }
    return lut;
}

juce::String shapeToString (const std::vector<ShapePoint>& pts)
{
    juce::StringArray parts;
    for (auto& p : pts) parts.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4) + "," + juce::String (p.curve, 3));
    return parts.joinIntoString (";");
}

std::vector<ShapePoint> shapeFromString (const juce::String& s)
{
    std::vector<ShapePoint> pts;
    for (auto& part : juce::StringArray::fromTokens (s, ";", ""))
    {
        auto v = juce::StringArray::fromTokens (part, ",", "");
        if (v.size() >= 2)
            pts.push_back ({ clamp01 (v[0].getFloatValue()), clamp01 (v[1].getFloatValue()), v.size() > 2 ? clampf (v[2].getFloatValue(), -1.0f, 1.0f) : 0.0f });
    }
    return pts;
}

std::vector<ShapePoint> defaultEnvShape()
{
    return { { 0.0f, 0.0f, 0.0f }, { 0.04f, 1.0f, -0.3f }, { 0.35f, 0.45f, 0.5f }, { 0.7f, 0.6f, 0.0f }, { 1.0f, 0.0f, 0.5f } };
}

std::vector<ShapePoint> defaultLfoShape()
{
    return { { 0.0f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.4f }, { 0.5f, 0.5f, -0.4f }, { 0.75f, 0.8f, 0.0f }, { 1.0f, 0.0f, 0.6f } };
}
} // namespace be
