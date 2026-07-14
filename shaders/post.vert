#version 450
layout(location = 0) out vec2 fragTexCoord;

void main() {
    // 메모리를 아끼기 위해 정점 버퍼 없이, 3개의 가상 정점만으로 화면 전체를 덮는 삼각형을 그리는 Vulkan 최적화 기법입니다.
    vec2 uvs[3] = vec2[](vec2(0.0, 0.0), vec2(2.0, 0.0), vec2(0.0, 2.0));
    fragTexCoord = uvs[gl_VertexIndex];
    gl_Position = vec4(uvs[gl_VertexIndex] * 2.0 - 1.0, 0.0, 1.0);
}