#version 450
layout(push_constant) uniform PushConstants {
    mat4 mvp;         // offset 0, size 64
    float alphaRef;   // offset 64, size 4 (unused in VS, needed for alignment)
    int alphaFunc;    // offset 68, size 4 (unused in VS, needed for alignment)
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    gl_Position.y = -gl_Position.y;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
