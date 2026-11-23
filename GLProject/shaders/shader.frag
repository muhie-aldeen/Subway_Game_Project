#version 330 core
// Receive from the vertex shader
in vec3 color;
in vec3 worldPos;
in vec2 texCoord;

// Uniform time sent from the CPU
uniform float time;
uniform vec3 cameraPosition;
uniform sampler2D diffuseTexture;
uniform bool useTexture;

// Output to screen
out vec4 FragColor;

//Note that we could have defined uniforms here and used them as well

void main()
{
    vec3 normal = normalize(cross(dFdx(worldPos), dFdy(worldPos)));
    if (!gl_FrontFacing)
        normal = -normal;

    vec3 lightDirection = normalize(vec3(-0.35, 0.75, -0.45));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float ambient = 0.42;
    vec3 baseColor = useTexture ? texture(diffuseTexture, texCoord).rgb : color;
    vec3 litColor = baseColor * (ambient + diffuse * 0.58);

    // نقوش برمجية بسيطة تظهر على الأرض والعوارض دون ملفات إكساء خارجية.
    float zPattern = smoothstep(0.48, 0.52, fract(worldPos.z * 0.25));
    float lanePattern = smoothstep(0.47, 0.53, fract(worldPos.x * 0.5));
    litColor *= 0.94 + zPattern * 0.04 + lanePattern * 0.025;

    vec3 fogColor = vec3(0.53, 0.81, 0.92);
    float distanceToCamera = length(worldPos - cameraPosition);
    float fogFactor = smoothstep(18.0, 48.0, distanceToCamera);
    litColor = mix(litColor, fogColor, fogFactor);

    FragColor = vec4(litColor, 1.0);
}
