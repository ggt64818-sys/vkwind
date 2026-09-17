#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

layout(location = 0) out vec4 vColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
};

void main() {
    gl_Position = mvp * vec4(aPos, 1.0);
    // D3D9 NDC: Y-up (bottom=-1, top=+1)
    // Vulkan NDC: Y-down (top=-1, bottom=+1)
    // Flip Y to match D3D9 convention
    gl_Position.y = -gl_Position.y;
    vColor = aColor;
}
