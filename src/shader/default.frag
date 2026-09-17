#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;         // offset 0, size 64 (unused in FS, needed for layout)
    float alphaRef;   // offset 64, size 4
    int alphaFunc;    // offset 68, size 4 (D3DCMP: 1=NEVER..8=ALWAYS, 0=disabled)
} pc;

layout(set = 0, binding = 3) uniform sampler2D tex0;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(tex0, fragTexCoord);
    outColor = texColor * fragColor;

    // Alpha test (D3DCMP values: 1=NEVER 2=LESS 3=EQUAL 4=LESSEQUAL 5=GREATER 6=NOTEQUAL 7=GEQUAL 8=ALWAYS)
    if (pc.alphaFunc == 0) {
        // alpha test disabled — do nothing
    } else if (pc.alphaFunc == 1) {
        discard; // NEVER
    } else if (pc.alphaFunc == 2 && outColor.a >= pc.alphaRef) {
        discard; // LESS: pass if a < ref
    } else if (pc.alphaFunc == 3 && outColor.a != pc.alphaRef) {
        discard; // EQUAL: pass if a == ref
    } else if (pc.alphaFunc == 4 && outColor.a > pc.alphaRef) {
        discard; // LESSEQUAL: pass if a <= ref
    } else if (pc.alphaFunc == 5 && outColor.a <= pc.alphaRef) {
        discard; // GREATER: pass if a > ref
    } else if (pc.alphaFunc == 6 && outColor.a == pc.alphaRef) {
        discard; // NOTEQUAL: pass if a != ref
    } else if (pc.alphaFunc == 7 && outColor.a < pc.alphaRef) {
        discard; // GEQUAL: pass if a >= ref
    }
    // ALWAYS (8) = never discard
}
