#version 440
// Word-by-word fill (docs/ui/pages/player.md §5.3): the sung part of a word at full
// strength, the rest at `dim`, with a soft edge `edge` wide (in item widths) between.
// progress 0 is all unsung, 1 all sung.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float progress;
    float edge;
    float dim;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    float front = mix(-edge, 1.0, progress);
    float lit = 1.0 - smoothstep(front, front + edge, qt_TexCoord0.x);
    fragColor = texture(source, qt_TexCoord0) * mix(dim, 1.0, lit) * qt_Opacity;
}
