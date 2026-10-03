#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(push_constant) uniform Transform { mat4 mvp; } transform;
layout(location = 0) out vec3 color;
void main() {
    gl_Position = transform.mvp * vec4(inPosition, 1.0);
    color = inColor;
}
