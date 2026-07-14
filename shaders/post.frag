#version 450
layout(binding = 0) uniform sampler2D texColor;  // 일반 우주 캔버스
layout(binding = 1) uniform sampler2D texBright; // 초고휘도 빛 캔버스

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = texture(texColor, fragTexCoord).rgb;
    vec3 bright = texture(texBright, fragTexCoord).rgb;
    
    // HDR 색상 합성
    vec3 hdrColor = color + bright;
    
    // [Reinhard Tone Mapping] 현실의 눈부신 빛을 모니터가 표현할 수 있는 색상으로 깎아냅니다.
    vec3 mapped = hdrColor / (hdrColor + vec3(1.0));
    
    outColor = vec4(mapped, 1.0);
}