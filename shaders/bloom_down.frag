#version 450

// 밉체인 블룸의 다운샘플 단계.
// 단순히 절반으로 줄이면 밝은 점이 프레임마다 깜빡이는 반딧불(firefly) 현상이 생긴다.
// 13탭 가중 샘플링으로 넓게 평균내면 그 깜빡임 없이 부드럽게 줄어든다.
// (Call of Duty: Advanced Warfare에서 소개된 방식)

layout(binding = 0) uniform sampler2D srcTex;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 srcTexel;  // 소스 텍스처의 1픽셀 크기(UV 단위)
    float radius;   // 업샘플 전용. 여기서는 쓰지 않는다.
} push;

void main() {
    vec2 t = push.srcTexel;
    vec2 uv = fragTexCoord;

    // 바깥 사각형(2텍셀 간격)
    vec3 a = texture(srcTex, uv + vec2(-2.0 * t.x,  2.0 * t.y)).rgb;
    vec3 b = texture(srcTex, uv + vec2( 0.0,        2.0 * t.y)).rgb;
    vec3 c = texture(srcTex, uv + vec2( 2.0 * t.x,  2.0 * t.y)).rgb;
    vec3 d = texture(srcTex, uv + vec2(-2.0 * t.x,  0.0)).rgb;
    vec3 e = texture(srcTex, uv).rgb;
    vec3 f = texture(srcTex, uv + vec2( 2.0 * t.x,  0.0)).rgb;
    vec3 g = texture(srcTex, uv + vec2(-2.0 * t.x, -2.0 * t.y)).rgb;
    vec3 h = texture(srcTex, uv + vec2( 0.0,       -2.0 * t.y)).rgb;
    vec3 i = texture(srcTex, uv + vec2( 2.0 * t.x, -2.0 * t.y)).rgb;

    // 안쪽 사각형(1텍셀 간격) — 가중치가 가장 크다
    vec3 j = texture(srcTex, uv + vec2(-t.x,  t.y)).rgb;
    vec3 k = texture(srcTex, uv + vec2( t.x,  t.y)).rgb;
    vec3 l = texture(srcTex, uv + vec2(-t.x, -t.y)).rgb;
    vec3 m = texture(srcTex, uv + vec2( t.x, -t.y)).rgb;

    // 가중치 합은 1.0이 되도록 맞춰져 있다(밝기가 단계마다 변하면 안 된다).
    vec3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;

    outColor = vec4(result, 1.0);
}
