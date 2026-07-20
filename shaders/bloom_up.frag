#version 450

// 밉체인 블룸의 업샘플 단계.
// 작은 밉을 3x3 텐트 필터로 확대해 한 단계 위 밉에 '더한다'(가산 블렌딩은 파이프라인이 담당).
// 여러 해상도의 번짐이 겹쳐 쌓이므로, 고정 반경 가우시안보다 훨씬 넓고 자연스러운 글로우가 된다.

layout(binding = 0) uniform sampler2D srcTex;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 srcTexel;  // 소스(더 작은 밉)의 1픽셀 크기(UV 단위)
    float radius;   // 텐트 필터 반경(소스 텍셀 배수). 클수록 번짐이 넓어진다.
} push;

void main() {
    vec2 t = push.srcTexel * push.radius;
    vec2 uv = fragTexCoord;

    vec3 a = texture(srcTex, uv + vec2(-t.x,  t.y)).rgb;
    vec3 b = texture(srcTex, uv + vec2( 0.0,  t.y)).rgb;
    vec3 c = texture(srcTex, uv + vec2( t.x,  t.y)).rgb;
    vec3 d = texture(srcTex, uv + vec2(-t.x,  0.0)).rgb;
    vec3 e = texture(srcTex, uv).rgb;
    vec3 f = texture(srcTex, uv + vec2( t.x,  0.0)).rgb;
    vec3 g = texture(srcTex, uv + vec2(-t.x, -t.y)).rgb;
    vec3 h = texture(srcTex, uv + vec2( 0.0, -t.y)).rgb;
    vec3 i = texture(srcTex, uv + vec2( t.x, -t.y)).rgb;

    // 텐트(3x3) 가중치 1-2-1 / 2-4-2 / 1-2-1, 합 16
    vec3 result = e * 4.0;
    result += (b + d + f + h) * 2.0;
    result += (a + c + g + i);
    result *= 1.0 / 16.0;

    outColor = vec4(result, 1.0);
}
