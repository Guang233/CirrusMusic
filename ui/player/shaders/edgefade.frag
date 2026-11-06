#version 440
// Fades the lyrics out toward the top and bottom edges of their column (player.md
// §5.2): `fadeTop` and `fadeBottom` are the fade heights in item heights.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float fadeTop;
    float fadeBottom;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    float y = qt_TexCoord0.y;
    float alpha = smoothstep(0.0, fadeTop, y) * (1.0 - smoothstep(1.0 - fadeBottom, 1.0, y));
    fragColor = texture(source, qt_TexCoord0) * alpha * qt_Opacity;
}
