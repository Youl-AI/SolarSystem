#version 450
layout(binding = 0) uniform sampler2D texColor;  // 일반 우주 캔버스
layout(binding = 1) uniform sampler2D texBright; // 초고휘도 빛 캔버스

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Push { float exposure; } push;

// [ACES Filmic Tone Mapping] Narkowicz 근사식. Reinhard보다 하이라이트 롤오프가
// 부드럽고 대비/채도가 영화적으로 살아난다.
vec3 acesFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// 밉체인 블룸은 6단계의 번짐을 전부 더해 쌓기 때문에, 기존의 고정 반경 가우시안 한 장보다
// 총 광량이 훨씬 커진다(태양이 눈에 띄게 밝아졌던 이유). 여기서 한 번 눌러 예전 밝기에 맞춘다.
const float kBloomStrength = 0.55;

void main() {
    vec3 color = texture(texColor, fragTexCoord).rgb;
    vec3 bright = texture(texBright, fragTexCoord).rgb;

    // HDR 색상 합성
    vec3 hdrColor = (color + bright * kBloomStrength) * push.exposure;

    vec3 mapped = acesFilmic(hdrColor);

    outColor = vec4(mapped, 1.0);
}