#version 450
layout(binding = 0) uniform sampler2D image;
layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    int horizontal;
} push;

void main() {
    // 1픽셀의 크기 구하기
    vec2 tex_offset = 1.0 / textureSize(image, 0); 
    
    // 블룸의 반경을 넓히기 위해 오프셋 간격을 2.5배로 벌립니다
    tex_offset *= 2.5; 

    // 가우시안 가중치 (중앙이 가장 밝고 멀어질수록 흐려짐)
    vec3 result = texture(image, fragTexCoord).rgb * 0.227027;
    
    // [수정됨] vec2가 아니라 순수한 float 거리 값으로 수정하여 y=0 참사 해결!
    float offset[4] = float[](1.0, 2.0, 3.0, 4.0);
    float weight[4] = float[](0.1945946, 0.1216216, 0.054054, 0.016216);

    // 가로 블러 (Ping)
    if(push.horizontal == 1) {
        for(int i = 0; i < 4; ++i) {
            result += texture(image, fragTexCoord + vec2(offset[i] * tex_offset.x, 0.0)).rgb * weight[i];
            result += texture(image, fragTexCoord - vec2(offset[i] * tex_offset.x, 0.0)).rgb * weight[i];
        }
    } 
    // 세로 블러 (Pong) - 이제 정상적으로 위아래로 빛이 퍼집니다!
    else {
        for(int i = 0; i < 4; ++i) {
            result += texture(image, fragTexCoord + vec2(0.0, offset[i] * tex_offset.y)).rgb * weight[i];
            result += texture(image, fragTexCoord - vec2(0.0, offset[i] * tex_offset.y)).rgb * weight[i];
        }
    }
    
    outColor = vec4(result, 1.0);
}