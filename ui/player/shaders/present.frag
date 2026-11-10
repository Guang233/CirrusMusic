#version 440
// Scales a smooth background up to full size, adds a faint dither (8-bit banding shows
// on large dark gradients) and rounds the corners like the window. With toneMap, a
// background not yet in the player's tone range (the blurred cover) is mapped into it
// first: each pixel's tone goes linearly into [toneLow, toneHigh], its hue kept and its
// chroma scaled (player.md §3.1), so the content keeps its contrast without a veil.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 boxSize; // item size in logical px
    float radius; // corner radius in logical px
    float toneMap;  // 1: map the source's tones into the range below
    float toneLow;  // L* 0–100
    float toneHigh;
    float chroma;   // chroma factor while mapping
};

layout(binding = 1) uniform sampler2D source;

// Interleaved gradient noise (Jimenez 2014): even, cheap, no texture.
float noise(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

float roundedBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

vec3 toLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 toSrgb(vec3 c)
{
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
float lstar(float y)
{
    return y > 0.008856 ? 116.0 * pow(y, 1.0 / 3.0) - 16.0 : 903.3 * y;
}
float luminance(float l)
{
    float f = (l + 16.0) / 116.0;
    return l > 8.0 ? f * f * f : l / 903.3;
}

void main()
{
    vec3 color = texture(source, qt_TexCoord0).rgb;
    if (toneMap > 0.5) {
        vec3 lin = toLinear(color);
        float y = dot(lin, vec3(0.2126, 0.7152, 0.0722));
        float target = luminance(mix(toneLow, toneHigh, clamp(lstar(y) / 100.0, 0.0, 1.0)));
        lin = vec3(y) + (lin - vec3(y)) * chroma; // around its own gray: hue kept
        lin = y > 1e-5 ? lin * (target / y) : vec3(target);
        color = toSrgb(clamp(lin, 0.0, 1.0));
    }
    color += (noise(gl_FragCoord.xy) - 0.5) / 255.0;
    vec2 p = (qt_TexCoord0 - 0.5) * boxSize;
    float inside = clamp(0.5 - roundedBox(p, boxSize * 0.5, radius), 0.0, 1.0);
    fragColor = vec4(color, 1.0) * inside * qt_Opacity;
}
