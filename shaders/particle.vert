#version 450

struct Particle {
    vec4 position; // xyz: 위치, w: 수명
    vec4 velocity;
    vec4 color;
};

// Phase 1에서 만든 10만 개의 창고를 그대로 읽어옵니다.
layout(binding = 0) readonly buffer ParticleSSBO {
    Particle particles[];
};

layout(binding = 1) uniform UBO {
    mat4 view; mat4 proj; vec3 cameraPos; float time; float deltaTime;
} ubo;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

void main() {
    // 10만 개 중 내 번호표 확인
    Particle p = particles[gl_InstanceIndex];
    
    // 사각형(Quad)을 구성하는 6개의 가상 좌표
    vec2 uvs[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 uv = uvs[gl_VertexIndex];
    fragUV = uv;
    
    float life = p.position.w; 
    fragColor = p.color;
    
    // 불꽃이 태어날 때 확 커졌다가, 죽을 때 스르륵 사라지도록 알파(투명도) 조절
    fragColor.a *= smoothstep(0.0, 0.3, life) * smoothstep(1.0, 0.7, life); 

    // 크기 (수명에 비례)
    float size = 0.25 * life; 
    
    // [핵심: 빌보딩] 무조건 카메라를 정면으로 바라보게 만듭니다!
    vec2 localPos = (uv * 2.0 - 1.0) * size;
    vec3 right = vec3(ubo.view[0][0], ubo.view[1][0], ubo.view[2][0]);
    vec3 upVec = vec3(ubo.view[0][1], ubo.view[1][1], ubo.view[2][1]);
    
    vec3 worldPos = p.position.xyz + right * localPos.x + upVec * localPos.y;
    
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}