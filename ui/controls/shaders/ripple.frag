#version 440
// Press ripple: a growing circle clipped to a rounded rectangle whose four
// corner radii are independent (connected button groups need that).

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 rippleColor;  // opaque color; strength carries the alpha
    vec2 boxSize;      // item size in logical px
    vec4 radii;        // top-left, top-right, bottom-right, bottom-left
    vec2 center;       // ripple origin in logical px
    float radius;      // ripple radius in logical px
    float strength;    // 0..1 opacity of the ripple
};

float roundedBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main()
{
    vec2 pos = qt_TexCoord0 * boxSize;
    vec2 p = pos - boxSize * 0.5;
    float r = p.x > 0.0 ? (p.y > 0.0 ? radii.z : radii.y) : (p.y > 0.0 ? radii.w : radii.x);
    r = min(r, min(boxSize.x, boxSize.y) * 0.5);
    float d = roundedBox(p, boxSize * 0.5, r);
    float aa = max(fwidth(d), 1e-4);
    float inside = clamp(0.5 - d / aa, 0.0, 1.0);
    float circle = clamp(0.5 + (radius - length(pos - center)) / aa, 0.0, 1.0);
    fragColor = vec4(rippleColor.rgb, 1.0) * (inside * circle * strength * qt_Opacity);
}
