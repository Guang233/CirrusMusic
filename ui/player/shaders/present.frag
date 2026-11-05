#version 440
// Scales a smooth background up to full size, adds a faint dither (8-bit banding shows
// on large dark gradients) and rounds the corners like the window.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 boxSize; // item size in logical px
    float radius; // corner radius in logical px
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

void main()
{
    vec3 color = texture(source, qt_TexCoord0).rgb + (noise(gl_FragCoord.xy) - 0.5) / 255.0;
    vec2 p = (qt_TexCoord0 - 0.5) * boxSize;
    float inside = clamp(0.5 - roundedBox(p, boxSize * 0.5, radius), 0.0, 1.0);
    fragColor = vec4(color, 1.0) * inside * qt_Opacity;
}
