#version 450

layout(location = 0) in vec3 inPosition;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    float time;
    vec3 sunPos; float sunRadius;
    vec3 earthPos; float earthRadius;
    vec3 moonPos; float moonRadius;
    mat4 lightSpaceMatrix;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    int objectType;
} push;

// 🚀 [NEW] 그림자를 그릴 때도 2만 개의 궤도 데이터가 동일하게 필요합니다.
layout(std140, binding = 8) readonly buffer AsteroidBuffer {
    mat4 models[];
} asteroids;

void main() {
    mat4 instanceModel = push.model;
    
    // 🚀 [NEW] 소행성(8)이면 자신의 번호표에 맞는 행렬을 곱해서 위치를 갱신합니다.
    if (push.objectType == 8) {
        instanceModel = push.model * asteroids.models[gl_InstanceIndex];
    }

    // 카메라 시점이 아닌, '태양의 시점(lightSpaceMatrix)'으로 2만 개의 소행성을 투영합니다.
    gl_Position = ubo.lightSpaceMatrix * instanceModel * vec4(inPosition, 1.0);
}