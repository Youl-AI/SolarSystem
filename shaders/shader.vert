#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view; mat4 proj; vec3 cameraPos; float time;
    vec3 sunPos; float sunRadius; vec3 earthPos; float earthRadius; vec3 moonPos; float moonRadius;
    vec4 occluders[16];
    vec4 occluderParams[16];
    int occluderCount;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model; int objectType;
} push;

// 🚀 [NEW] C++에서 넘겨주는 2만 개의 행렬 데이터를 받을 SSBO
layout(std140, binding = 7) readonly buffer AsteroidBuffer {
    mat4 models[];
} asteroids;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) flat out int fragObjectType;
layout(location = 5) out vec3 fragTexCube;

void main() {
    fragColor = inColor;
    fragTexCoord = inTexCoord;

    // 🚀 [핵심] 기본 모델 행렬을 복사한 뒤, 소행성(8)이면 인스턴스 번호에 맞는 행렬을 곱해줍니다!
    mat4 instanceModel = push.model;
    if (push.objectType == 8) {
        instanceModel = push.model * asteroids.models[gl_InstanceIndex];
    }

    // 빛 반사(Normal)를 위한 찌그러짐 계산도 변경된 instanceModel을 기준으로 수행
    mat3 normalMatrix = transpose(inverse(mat3(instanceModel)));
    fragNormal = normalMatrix * inNormal;
    
    fragObjectType = push.objectType;
    fragTexCube = inPosition;
    
    if (push.objectType == 3) {
        // 스카이박스
        mat4 rotView = mat4(mat3(ubo.view)); 
        vec4 pos = ubo.proj * rotView * instanceModel * vec4(inPosition, 1.0);
        gl_Position = pos.xyww;
        fragPos = vec3(0.0);
    } else {
        // 일반 천체 및 2만 개의 소행성들 렌더링
        vec4 worldPos = instanceModel * vec4(inPosition, 1.0);
        gl_Position = ubo.proj * ubo.view * worldPos;
        fragPos = worldPos.xyz;
    }
}