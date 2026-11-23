#version 330 core
// Stored in the VBO
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 vertexColor;
layout(location = 2) in vec2 aTexCoord;

// Uniform matrices sent from the CPU
uniform mat4 model, view, perspective;

// Uniform time sent from the CPU
uniform float time;
uniform vec3 cameraPosition;

// Send to the fragment shader
out vec3 color;
out vec3 worldPos;
out vec2 texCoord;

void main() {
    color = vertexColor;
    vec4 worldPosition = model * vec4(pos, 1.0);
    worldPos = worldPosition.xyz;
    texCoord = aTexCoord;
    gl_Position = perspective * view * worldPosition;
}
