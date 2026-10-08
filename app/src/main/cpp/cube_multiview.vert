#version 450
#extension GL_EXT_multiview : require
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(push_constant) uniform Transform { mat4 mvp[2]; } transform;
layout(location = 0) out vec3 color;
void main() {
    gl_Position = transform.mvp[gl_ViewIndex] * vec4(inPosition, 1.0);
    color = inColor;
}
