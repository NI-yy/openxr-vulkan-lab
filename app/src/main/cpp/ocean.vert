#version 450
layout(location = 0) out vec2 uv;
layout(location = 1) flat out int eyeIndex;
void main() {
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    uv = corner;
    eyeIndex = 0;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.999, 1.0);
}
