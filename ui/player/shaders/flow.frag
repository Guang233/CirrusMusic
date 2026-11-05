#version 440
// The player's flowing light (docs/ui/pages/player.md §3.2): 25 soft color points, one
// per cell of the cover's 5 x 5 colors, each drifting on its own slow curve while the
// whole set turns once in about 60 s. Each pixel blends the points by Gaussian weight
// in linear light. Rendered small; present.frag scales it up.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;   // seconds of flow (slows to a stop while paused)
    float aspect; // width / height
};

// The 5 x 5 colors (texel (x, y) is cell x, y), sampled at the texel centers (no
// texelFetch or integer ops: the shader also bakes to GLSL ES 100).
layout(binding = 1) uniform sampler2D colors;

float hash(float n)
{
    return fract(sin(n * 12.9898) * 43758.5453);
}

void main()
{
    const float turn = 6.2831853 / 60.0;
    float angle = time * turn;
    mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    // A slow, low-frequency warp bends the edges between the colors into curves.
    vec2 uv = qt_TexCoord0;
    uv += 0.07 * vec2(sin(uv.y * 3.1 + time * 0.23), cos(uv.x * 2.7 - time * 0.19));
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int i = 0; i < 25; ++i) {
        float fi = float(i);
        vec2 cell = vec2(mod(fi, 5.0), floor(fi / 5.0));
        // Spread past the edges so the turning set always covers the corners.
        vec2 base = ((cell + 0.5) / 5.0 - 0.5) * 1.35;
        vec2 drift = 0.16 * vec2(sin(time * (0.16 + 0.14 * hash(fi)) + 6.2831853 * hash(fi + 7.0)),
                                 cos(time * (0.15 + 0.13 * hash(fi + 3.0)) + 6.2831853 * hash(fi + 11.0)));
        vec2 p = rotation * (base + drift) + 0.5;
        vec2 d = vec2((uv.x - p.x) * aspect, uv.y - p.y);
        float w = exp(-dot(d, d) / 0.035);
        vec3 c = texture(colors, (cell + 0.5) / 5.0).rgb;
        sum += pow(c, vec3(2.2)) * w;
        total += w;
    }
    vec3 color = pow(sum / max(total, 1e-20), vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0) * qt_Opacity;
}
