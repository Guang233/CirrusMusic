#version 440
// The active part of a wavy progress bar (MD3E): a sine stroke with round caps along
// the item's horizontal center, from its left edge to its right edge.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 strokeColor;  // premultiplied by Qt
    vec2 boxSize;      // item size in logical px
    float thickness;   // stroke width in px
    float amplitude;   // peak offset from the center line in px
    float wavelength;  // px
    float phase;       // px the wave has moved forward
    float pixel;       // one device pixel in logical px, for the edge
};

const float kTau = 6.28318530718;

float waveY(float x)
{
    return boxSize.y * 0.5 + amplitude * sin(kTau * (x - phase) / wavelength);
}

void main()
{
    vec2 p = qt_TexCoord0 * boxSize;
    float r = thickness * 0.5;
    float x0 = r;
    float x1 = max(r, boxSize.x - r);
    float d;
    if (p.x < x0 || p.x > x1) {
        // The round caps.
        float cx = clamp(p.x, x0, x1);
        d = length(p - vec2(cx, waveY(cx)));
    } else {
        // Distance to the curve, to first order: the vertical gap over the slope.
        float slope = amplitude * kTau / wavelength * cos(kTau * (p.x - phase) / wavelength);
        d = abs(p.y - waveY(p.x)) / sqrt(1.0 + slope * slope);
    }
    float alpha = clamp((r - d) / pixel + 0.5, 0.0, 1.0);
    fragColor = strokeColor * (alpha * qt_Opacity);
}
