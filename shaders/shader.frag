#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) flat in int fragObjectType;
layout(location = 5) in vec3 fragTexCube;

layout(binding = 0) uniform UniformBufferObject {
    mat4 view; mat4 proj; vec3 cameraPos; float time;
    vec3 sunPos; float sunRadius; vec3 earthPos; float earthRadius; vec3 moonPos; float moonRadius;
    vec4 occluders[16];       // xyz = 중심(카메라 상대), w = 반지름. MAX_OCCLUDERS와 반드시 일치.
    vec4 occluderParams[16];  // x = 이 천체의 그림자에 쓸 태양 반지름(렌더되는 크기와 다르다)
    int occluderCount;
} ubo;

layout(binding = 1) uniform sampler2D texDiffuse;
layout(binding = 2) uniform sampler2D texNight;
layout(binding = 3) uniform sampler2D texSpecular;
layout(binding = 4) uniform sampler2D texNormalDisp;
layout(binding = 5) uniform sampler2D texClouds;
layout(binding = 6) uniform samplerCube skyboxTex;   // hiptyc: 히파르코스/티코의 밝은 별
layout(binding = 8) uniform samplerCube skyBandTex;  // milkyway: 은하수 확산광 + 어두운 별

// ── 실제 축척에서의 하늘 상수 ─────────────────────────────────────────────
// 눈대중이 아니라 scratchpad/sky_calibrate.py의 측정으로 정했다.
//
// 배율 0.593: 국소 최대점이 전천 9,100개(대기 없는 우주의 맨눈 한계 6.5등급)가 되는
//   원시값 문턱이 0.2578이었다(실측 9,102개). 그 문턱이 화면 가시 문턱 0.02에
//   걸리도록 맞춘 값이라, 그보다 어두운 별은 화면에서 사라진다.
//
// 감마 2.5: 여기엔 타협이 있다. 원본이 1.0에서 클리핑돼 있어 가장 어두운 별과 가장
//   밝은 별의 비가 5.7배뿐인데(실제로는 6.5등급과 -1.5등급 사이가 1585배), 이 좁은
//   범위를 감마로 늘려야 한다.
//     - 개수 분포를 실제와 맞추려면 감마 0.82다. 관측된 전천 별 수는 등급당 10^0.50배씩
//       늘어 N ~ F^-1.25인데, 이 텍스처는 N ~ v^-1.026이라 그 비를 맞추는 값이다.
//       그런데 이러면 가장 밝은 별이 화면에서 0.084라 눈에 띄는 별이 하나도 없다.
//     - 밝은 별이 제대로 빛나려면(1.5 이상) 감마 2.5가 필요하다. 대신 개수 분포의
//       기울기가 0.41로 실제(1.25)보다 완만해진다.
//   별 개수 자체는 어느 쪽이든 9,100개로 같다(문턱이 정하므로). 감마는 그 위쪽의
//   밝기 분포만 바꾼다. 밝은 별이 보이는 쪽을 택했다.
#define SKY_STAR_GAMMA  2.5
#define SKY_STAR_SCALE  0.593

// 은하수 층 배율 3.087: 띠가 화면에서 0.07이 되게 한다. 실제 은하수는 밤하늘 배경보다
//   2~4배 밝은 정도의 희미한 띠다.
//
// LOD 편향 2.0: 이 층에는 확산광뿐 아니라 어두운 Gaia 별도 들어 있어서, 그냥 배율만
//   곱하면 그 별들이 되살아나 개수가 두 배가 된다(전천 9,083개 추가). 감마로는 못 막는다 —
//   띠(0.027)가 그 별들보다 어두워서 감마를 걸면 띠가 먼저 사라진다.
//   낮은 밉을 쓰면 해결된다. 실제로 맨눈에는 어두운 별이 분해되지 않고 은하수
//   확산광으로 뭉쳐 보이므로, 물리적으로도 이쪽이 맞다.
//   측정: 밉 2단계에서 분해되는 별이 9,083 -> 339개로 줄고 띠 밝기는 14%만 떨어진다.
//   textureLod가 아니라 편향을 쓰는 이유는 화면 축소에 따른 자동 밉 선택을 남기기 위해서다.
#define SKY_BAND_SCALE  3.087
#define SKY_BAND_LOD    2.0

// 탈채도 0.7: 띠의 평균색이 (0.0195, 0.0250, 0.0368)로 푸른 쪽에 치우쳐 있다.
// 실제 은하수는 눈의 색각 문턱보다 어두워 회백색으로 보인다. 완전히 빼면 죽은 회색이라
// 살짝 남긴다.
#define SKY_BAND_DESAT  0.7

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBrightColor; 

layout(push_constant) uniform PushConstants { mat4 model; int objectType; float customData;} push;

float hash(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.x + p.y) * p.z); }
float noise3D(vec3 x) { vec3 p = floor(x); vec3 f = fract(x); f = f * f * (3.0 - 2.0 * f); return mix(mix(mix(hash(p+vec3(0,0,0)),hash(p+vec3(1,0,0)),f.x),mix(hash(p+vec3(0,1,0)),hash(p+vec3(1,1,0)),f.x),f.y),mix(mix(hash(p+vec3(0,0,1)),hash(p+vec3(1,0,1)),f.x),mix(hash(p+vec3(0,1,1)),hash(p+vec3(1,1,1)),f.x),f.y),f.z); }
float fbm(vec3 x) { float v = 0.0; float a = 0.5; vec3 shift = vec3(100.0); for(int i=0; i<4; ++i){ v+=a*noise3D(x); x=x*2.0+shift; a*=0.5; } return v; }

// ── 태양 홍염(type 7)의 상한 유도 ──────────────────────────────────────────
// noise3D는 [0,1]이고 fbm은 진폭 0.5+0.25+0.125+0.0625를 더하므로 fbm <= 0.9375다.
// 홍염 반지름은 flameRadius = 1.01 + flamesShape * 0.15이고
//   flamesShape = pow(chaoticNoise, 3) * 3 * (0.7 + 0.6 * fineDetail)
//   chaoticNoise = n1*0.7 + n2*0.3   (n1, n2, fineDetail 모두 fbm)
// 이 상한들을 대입하면 flameRadius <= 1.478이다. 그보다 바깥 픽셀은 알파가 정확히 0이다.
//
// 이 상한을 중간 단계에도 적용하면, 아직 안 구한 fbm에 최대값을 넣어 '이 픽셀은 무슨
// 수를 써도 불꽃이 닿지 않는다'를 미리 판정할 수 있다. 판정이 보수적이라 화면은 그대로다.
#define FBM_MAX      0.9375
#define FINE_MAX     (0.7 + 0.6 * FBM_MAX)          // fineDetail 변조의 상한 = 1.2625
#define FLAME_R(s)   (1.01 + (s) * 0.15)            // flamesShape -> flameRadius
#define SHAPE(c)     (pow(c, 3.0) * 3.0 * FINE_MAX) // chaoticNoise -> flamesShape 상한
#define FLAME_R_MAX  FLAME_R(SHAPE(FBM_MAX))        // = 1.478

vec3 calculateNormal(vec2 uv, vec3 baseNormal) {
    // z는 저장하지 않고 복원한다. 노말맵은 구울 때 단위 벡터로 만들어 두었으므로
    // z = sqrt(1 - x^2 - y^2)가 정확히 성립한다. 덕분에 두 채널만 담는 BC5를 쓸 수 있고,
    // BC5는 두 채널을 온전히 x·y에 배정해서 BC7보다 법선 오차가 절반이다
    // (측정: 수성 0.51도 대 0.89도, 달 1.32도 대 2.51도).
    // 압축하지 않은 RGB 노말맵(달)도 같은 식으로 복원되므로 경로가 하나로 유지된다.
    vec2 nxy = texture(texNormalDisp, uv).xy * 2.0 - 1.0;
    vec3 tangentNormal = vec3(nxy, sqrt(max(0.0, 1.0 - dot(nxy, nxy))));
    // 증폭 배수는 천체마다 다르다(Planet::normalAmp 참조). 실측 DEM 맵은 8비트 정밀도를
    // 살리려고 구울 때 미리 증폭해 두었으므로 여기서는 그만큼 덜 키운다.
    // customData를 안 넘기는 경로가 생기면 0이 들어와 요철이 사라지므로 예전 값으로 되돌린다.
    float amp = push.customData > 0.0 ? push.customData : 5.0;
    tangentNormal.xy *= amp; tangentNormal = normalize(tangentNormal);
    vec3 N = normalize(baseNormal); vec3 up = normalize(vec3(0.0, 1.0, 0.001));
    vec3 T = normalize(cross(up, N)); vec3 B = cross(N, T);
    return normalize(mat3(T, B, N) * tangentNormal);
}

void writeOut(vec4 color) {
    outColor = color;
    float brightness = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    if (brightness > 1.0) {
        // 🚀 [핵심 1] 빛 번짐 버퍼로 넘어가는 최대 에너지를 5.0으로 제한하여 화면을 덮어버리는 대참사를 막습니다.
        outBrightColor = vec4(min(color.rgb, vec3(5.0)), color.a); 
    } else {
        outBrightColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}

// 고리가 행성 본체에 드리우는 그림자.
// 픽셀에서 태양으로 광선을 쏘아 고리 평면(행성 적도면)과 만나는 지점을 구하고,
// 그 반지름이 고리 범위 안이면 고리 텍스처의 알파만큼 빛을 가린다.
// 고리 텍스처는 texClouds 슬롯으로 들어온다(가스 행성은 구름 맵을 쓰지 않는다).
// 토성이 아닌 행성은 이 슬롯이 알파 0인 더미라 그림자가 생기지 않는다.
float RingShadow(vec3 pixelPos, vec3 sunPos, mat4 model, float ringInner, float ringOuter) {
    vec3 center = vec3(model[3]);
    // 모델 행렬의 Y축 = 행성의 자전축(자전 기울기 포함). 고리는 이 축에 수직인 평면에 있다.
    vec3 planeN = normalize(vec3(model[1]));
    float planetScale = length(vec3(model[0]));

    vec3 L = normalize(sunPos - pixelPos);
    float denom = dot(L, planeN);
    if (abs(denom) < 1e-5) return 1.0;              // 광선이 고리면과 평행

    float t = dot(center - pixelPos, planeN) / denom;
    if (t <= 0.0) return 1.0;                        // 고리면이 태양 반대쪽에 있다

    vec3 hit = pixelPos + L * t;
    float r = length(hit - center) / planetScale;    // 모델 단위로 환산
    if (r < ringInner || r > ringOuter) return 1.0;

    float u = (r - ringInner) / (ringOuter - ringInner);
    // 이 함수는 조기 반환이 있어 비균일 제어 흐름이다. 암묵적 미분(밉 선택)은 여기서
    // 정의되지 않으므로 LOD를 명시적으로 0으로 고정한다.
    // 밉맵 0단계는 2048px이라 방사 줄무늬가 표면에 페인트처럼 도드라진다. 그림자는 원래
    // 반영본影이라 윤곽만 남는 게 자연스러우므로, 낮은 밉을 읽어 미세 줄무늬를 뭉갠다.
    float density = textureLod(texClouds, vec2(u, 0.5), 4.0).a;
    return 1.0 - density * 0.45;                     // 고리는 반투명이라 절반 이하만 가린다
}

// 반지름 R인 원반이 반지름 r인 원반에 중심 간격 d만큼 떨어져 겹칠 때, R이 가려지는 면적 비율.
// 두 원의 렌즈꼴 교차 면적 공식이라 근사가 아니라 정확한 값이다.
float DiscOverlapFraction(float R, float r, float d) {
    if (d >= R + r) return 0.0;              // 안 겹침
    if (d <= r - R) return 1.0;              // 개기: 가리는 쪽이 태양을 통째로 덮는다
    if (d <= R - r) return (r * r) / (R * R); // 금환: 가리는 쪽이 태양 안에 쏙 들어간다

    float d2 = d * d, R2 = R * R, r2 = r * r;
    float a1 = acos(clamp((d2 + R2 - r2) / (2.0 * d * R), -1.0, 1.0));
    float a2 = acos(clamp((d2 + r2 - R2) / (2.0 * d * r), -1.0, 1.0));
    float tri = 0.5 * sqrt(max(0.0, (-d + r + R) * (d + r - R) * (d - r + R) * (d + r + R)));
    return clamp((R2 * a1 + r2 * a2 - tri) / (3.14159265 * R2), 0.0, 1.0);
}

// 이 지점에서 봤을 때 태양 원반이 가려진 비율(0 = 완전히 밝음, 1 = 개기).
//
// 셰도우 맵을 대체한다. 천체가 전부 구라서 광선-구 교차로 정확히 풀리고,
// 텍셀이라는 개념이 없으므로 아무리 확대해도 그림자 경계에 계단이 생기지 않는다.
// 태양을 점이 아니라 원반으로 두기 때문에 본영과 반영이 공짜로, 물리적으로 맞게 나온다.
float SunDiscOcclusion(vec3 P) {
    vec3 toSun = ubo.sunPos - P;
    float dSun = length(toSun);
    if (dSun < 1e-6) return 0.0;
    vec3 sunDir = toSun / dSun;

    float covered = 0.0;
    for (int i = 0; i < ubo.occluderCount; ++i) {
        vec3 C = ubo.occluders[i].xyz;
        float r = ubo.occluders[i].w;

        // 태양의 각반지름은 천체마다 다르다. 화면에 그려지는 태양은 연출상 크게 두되,
        // 그림자 기하는 실제 비율을 쓰려고 천체별로 축소된 반지름을 넘겨받는다.
        // (자세한 이유는 main.cpp의 SHADOW_SUN_SHRINK 주석 참고)
        float aSun = asin(clamp(ubo.occluderParams[i].x / dSun, 0.0, 1.0));
        if (aSun <= 0.0) continue;

        vec3 toOcc = C - P;
        float dOcc = length(toOcc);

        // 자기 자신은 건너뛴다. 표면 위의 점은 자기 중심에서 정확히 반지름만큼 떨어져 있다.
        // 자기 그림자(밤면)는 어차피 NdotL이 처리하므로 여기서 볼 필요가 없다.
        if (dOcc <= r * 1.001) continue;
        if (dOcc >= dSun) continue;                    // 태양보다 멀면 가릴 수 없다

        float cosSep = dot(sunDir, toOcc / dOcc);
        if (cosSep <= 0.0) continue;                   // 태양 반대편에 있다

        float aOcc = asin(clamp(r / dOcc, 0.0, 1.0));  // 가리는 천체의 각반지름
        float sep = acos(clamp(cosSep, -1.0, 1.0));    // 태양 중심과의 각거리

        // 합이 아니라 최댓값. 두 위성이 겹쳐 보일 때 1을 넘겨버리는 걸 막고,
        // 실제로도 거의 항상 하나가 지배적이다.
        covered = max(covered, DiscOverlapFraction(aSun, aOcc, sep));
    }
    return covered;
}

// ── 고배율 디테일 ──────────────────────────────────────────────────────────
// 8K 텍스처라도 표면에 바짝 붙으면 텍셀 하나가 여러 픽셀로 늘어나 죽처럼 뭉개진다.
// 늘어난 정도를 화면 미분(fwidth)으로 직접 재서, 텍셀보다 픽셀이 촘촘해지는 구간에서만
// 절차적 노이즈를 섞는다. 멀리서는 계수가 0이라 원본 텍스처가 한 톨도 바뀌지 않는다.
//
// 노이즈 텍스처를 따로 두지 않고 태양 표면에 쓰던 fbm을 그대로 재활용하므로 VRAM은 0이다.
// SpaceEngine이 실측 베이스맵 아래를 절차적으로 채우는 것과 같은 발상이고, 다만 여기서는
// 지형을 지어내지 않고 이미 있는 색을 잘게 흔들어 뭉갬만 가린다.
float DetailFactor(vec2 uv, sampler2D tex) {
    // fwidth는 화면상 이웃 픽셀과의 차이다. 텍스처 크기를 곱하면 '픽셀당 텍셀 수'가 된다.
    // 1보다 작다는 건 텍셀 하나가 여러 픽셀에 걸쳐 늘어났다는 뜻이다.
    vec2 texelsPerPixel = fwidth(uv) * vec2(textureSize(tex, 0));
    return 1.0 - smoothstep(0.15, 1.0, max(texelsPerPixel.x, texelsPerPixel.y));
}

// UV가 아니라 구면 법선으로 노이즈를 만든다. UV를 쓰면 극지방에서 경선이 모이며
// 무늬가 함께 뭉쳐 눈에 띄는 특이점이 생긴다.
vec3 ApplyDetail(vec3 albedo, vec3 sphereNormal, float amount) {
    if (amount <= 0.0) return albedo;
    // 주파수는 텍스처 텍셀보다 잘아야 의미가 있다. 8K 지구는 텍셀 하나가 약 4.9km인데,
    // 4000이면 첫 옥타브가 약 1.6km, 마지막 옥타브가 약 200m라 그 아래를 메운다.
    float n = fbm(sphereNormal * 4000.0);
    return albedo * mix(1.0, 0.78 + 0.44 * n, amount);
}

// 조명에 곱할 밝기 계수(1 = 그늘 없음, 0 = 완전한 그늘).
float SunVisibility(vec3 P) { return 1.0 - SunDiscOcclusion(P); }

// ── 대기 산란 (레일리 단일 산란) ──────────────────────────────────────────
// 지금까지 대기는 가장자리에 프레넬로 파란 띠를 칠하는 눈속임이었다. 그 방식은 빛이
// 대기를 얼마나 통과했는지를 모르기 때문에, 노을의 붉은색을 따로 손으로 칠해줘야 했다.
//
// 여기서는 시선을 따라 대기 밀도를 적분하고, 각 지점마다 태양까지의 광학 두께를 구한다.
// 파장이 짧을수록 강하게 산란되므로(1/λ^4) 한낮의 하늘은 파랗고, 빛이 대기를 비스듬히
// 길게 지나는 새벽·노을에는 파란빛이 먼저 다 흩어져 붉은빛만 남는다. 손으로 칠하지
// 않아도 기하가 알아서 만들어낸다.

const vec3  kRayleigh     = vec3(5.8, 13.5, 33.1); // 1/λ^4 비율 (680 / 550 / 440nm)
// 연직 광학 두께가 실제 지구와 맞도록 잡은 값. 이 상수에 kRayleigh와 척도 높이를 곱하면
// R 0.040 / G 0.093 / B 0.228이 나오는데, 실제 지구는 0.026 / 0.098 / 0.230이다.
// 예전 값 1.4는 0.024 / 0.057 / 0.139로 초록·파랑이 실제의 절반이었다.
const float kAtmoDensity  = 2.3;
// 산란광 밝기. 광학 두께를 올린 만큼 낮춰 화면 밝기는 그대로 둔다(0.4 x 0.63).
const float kAtmoBright   = 0.25;
// 22.0으로 시작했다가 화면이 통째로 흰 공이 됐다. 시선이 대기를 비스듬히 길게 지나는
// 가장자리에서는 광학 경로가 중심의 18배까지 길어져, 중심을 기준으로 맞추면 림이
// 15를 넘겨 톤매퍼와 블룸을 동시에 포화시킨다. 림이 1.4 근처에 오도록 잡은 값이다.
// 밀도를 대신 올리면 소광이 세져 림이 하얗게 바래므로, 파란 대기를 원하면 밝기로 잡아야 한다.
// 에어로졸(미 산란). 먼지·연무·바다 소금 입자는 공기 분자보다 훨씬 커서 파장을 덜 가리고
// (옹스트롬 지수 1.3, 레일리의 4에 비해 완만하다) 빛을 앞쪽으로 몰아 보낸다. 그래서
// 태양을 등지고 보는 원반 한복판은 거의 바뀌지 않고, 빛이 스치듯 지나는 명암 경계와
// 가장자리에서 연무가 밝게 뜬다. 실제 하늘에서 해 질 녘 지평선이 뿌옇게 빛나는 그 현상이다.
//
// 연직 광학 두께가 550nm에서 0.1이 되도록 잡았다(전 지구 평균값). 계수 = 0.1 / 척도높이.
//
// 0.2도 시도해 봤다(실제 범위 안의 뿌연 대기). 노을이 진해지지는 않고 가장자리가 그냥
// 허옇게 밝아지기만 했다. 에어로졸은 파장을 거의 안 가려서 흰 연무를 더할 뿐이고,
// 노을의 붉은색은 레일리 소광이 파란빛을 걷어내면서 나오기 때문이다.
const vec3  kMie          = vec3(60.7, 80.0, 106.9);
const float kMieG         = 0.76;                    // 전방 산란 쏠림 정도
// 실제 에어로졸은 레일리보다 훨씬 낮게(척도높이 1.2km 대 8.5km) 깔리지만, 시선 적분이
// 16단계뿐이라 그만큼 얇게 잡으면 층을 건너뛰어 띠가 생긴다. 레일리의 0.42배로 타협했다.
const float kMieHeightMul = 0.05;
const float kAtmoShellScale = 1.025;                 // main.cpp의 kAtmosphereShellScale과 일치

// 광선-구 교차. x = 가까운 t, y = 먼 t. 만나지 않으면 x > y가 되어 판별에 쓸 수 있다.
vec2 raySphere(vec3 ro, vec3 rd, vec3 c, float r) {
    vec3 oc = ro - c;
    float b = dot(oc, rd);
    float h = b * b - (dot(oc, oc) - r * r);
    if (h < 0.0) return vec2(1.0, -1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

vec3 AtmosphereScattering(vec3 ro, vec3 rd, vec3 center, float planetR, float atmoR) {
    vec2 atmo = raySphere(ro, rd, center, atmoR);
    if (atmo.x > atmo.y) return vec3(0.0);

    float tStart = max(atmo.x, 0.0);
    float tEnd   = atmo.y;

    // 지면에 먼저 닿으면 거기서 끊는다. 땅에 가려진 대기는 보이지 않는다.
    vec2 ground = raySphere(ro, rd, center, planetR);
    if (ground.x <= ground.y && ground.x > 0.0) tEnd = min(tEnd, ground.x);
    if (tEnd <= tStart) return vec3(0.0);

    // 밀도가 e배 줄어드는 높이. 껍질 두께에 비례시켜 두면 리얼 스케일로 넘어가도
    // 모양이 유지된다. 길이를 전부 행성 반지름으로 나눠 쓰는 것도 같은 이유다.
    //
    // 0.25로 시작했다가 대기가 반지름의 0.63%까지 부풀었다(실제 지구는 0.13%).
    // 그러면 밝은 부분이 가장자리에 몰리지 않고 원반 전체에 퍼져 지표면을 뿌옇게 덮는다.
    // 0.12면 밝은 띠가 표면 바로 위에 붙어, 우주에서 보는 그 얇고 푸른 지평선이 된다.
    float thickness   = atmoR - planetR;
    float scaleHeight = thickness * 0.12;

    // 밀도가 표면 근처에 몰려 있어, 성기게 잡으면 그 층을 건너뛰어 얼룩이 생긴다.
    const int VIEW_STEPS  = 16;
    const int LIGHT_STEPS = 6;

    float scaleHeightMie = thickness * kMieHeightMul;

    float ds = (tEnd - tStart) / float(VIEW_STEPS);
    vec3  accumRay = vec3(0.0);
    vec3  accumMie = vec3(0.0);
    float viewOptical = 0.0;
    float viewOpticalMie = 0.0;

    // 일식으로 태양이 가려지면 산란시킬 빛도 줄어든다. 시선 전체에 한 번만 적용해도
    // 충분하다 — 대기 두께에 걸쳐 가림 정도가 눈에 띄게 달라지지는 않는다.
    float sunVis = SunVisibility(ro + rd * ((tStart + tEnd) * 0.5));

    for (int i = 0; i < VIEW_STEPS; ++i) {
        vec3  P = ro + rd * (tStart + (float(i) + 0.5) * ds);
        float h = length(P - center) - planetR;
        float density    = exp(-h / scaleHeight)    * (ds / planetR);
        float densityMie = exp(-h / scaleHeightMie) * (ds / planetR);
        viewOptical    += density;
        viewOpticalMie += densityMie;

        vec3 L = normalize(ubo.sunPos - P);

        // 태양에서 이 지점까지 빛이 지나온 대기의 양.
        //
        // 예전에는 광선이 행성에 닿으면 샘플을 통째로 버렸다. 그러면 명암 경계가 칼로 자른
        // 듯 끊겨 노을이 한 점에서만 나타난다. 대신 경로를 그대로 적분한다 — 지면 아래로
        // 파고든 구간은 고도가 음수라 밀도가 폭발적으로 커지고, 소광이 저절로 무한대에
        // 가까워진다. 밤면은 여전히 어둡지만 그 경계가 부드럽게 이어진다.
        vec2  lightExit = raySphere(P, L, center, atmoR);
        float lds = max(lightExit.y, 0.0) / float(LIGHT_STEPS);
        float lightOptical = 0.0;
        float lightOpticalMie = 0.0;
        for (int j = 0; j < LIGHT_STEPS; ++j) {
            vec3 Q = P + L * ((float(j) + 0.5) * lds);
            float hAbs = length(Q - center) - planetR;
            // 지면 아래에서 지수가 폭주해 inf/NaN이 되지 않도록 상한을 둔다.
            lightOptical    += exp(-clamp(hAbs / scaleHeight,    -12.0, 60.0)) * (lds / planetR);
            lightOpticalMie += exp(-clamp(hAbs / scaleHeightMie, -12.0, 60.0)) * (lds / planetR);
        }

        // 태양 -> 이 지점 -> 눈까지 오는 동안 흩어져 사라진 만큼을 감쇠시킨다.
        // 파란빛이 먼저 사라지므로, 빛이 대기를 길게 지나온 명암 경계에서는 붉은빛만 남는다.
        vec3 tau = min(kRayleigh * kAtmoDensity * (viewOptical + lightOptical)
                     + kMie * (viewOpticalMie + lightOpticalMie), vec3(80.0));
        vec3 trans = exp(-tau);
        accumRay += density    * trans;
        accumMie += densityMie * trans;
    }

    // mu는 입사 진행 방향과 산란 후 진행 방향이 이루는 각의 코사인이다. 태양을 등지고
    // 볼 때 -1(후방 산란), 태양 쪽 가장자리를 스쳐볼 때 +1(전방 산란)이 된다.
    float mu = dot(rd, normalize(ubo.sunPos - ro));

    // 레일리 위상함수. 전방/후방 산란이 측방보다 강해서 역광일 때 대기가 링처럼 빛난다.
    float phaseRay = 0.75 * (1.0 + mu * mu);
    // 헤니-그린스타인. 전방으로 강하게 쏠려, 태양 쪽 가장자리에서만 연무가 밝게 뜬다.
    float g2 = kMieG * kMieG;
    float phaseMie = (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * kMieG * mu, 1e-4), 1.5);

    vec3 scattered = accumRay * kRayleigh * kAtmoDensity * phaseRay
                   + accumMie * kMie * phaseMie;
    return scattered * kAtmoBright * sunVis;
}

void main() {
    gl_FragDepth = gl_FragCoord.z;

    vec3 baseNormal = normalize(fragNormal);
    vec3 lightDir = normalize(ubo.sunPos - fragPos); 
    vec3 viewDir = normalize(ubo.cameraPos - fragPos);

    if (fragObjectType == 11) {
        // 대기 껍질. 이 조각은 시선이 대기로 '들어가는 지점'일 뿐이고,
        // 실제 색은 거기서부터 지면(또는 반대편 껍질)까지 적분해서 만든다.
        // 껍질이 행성보다 크므로 실루엣 바깥으로 삐져나온 부분이 대기 링이 된다.
        vec3 center  = vec3(push.model[3]);
        float atmoR  = length(vec3(push.model[0]));
        float planetR = atmoR / kAtmoShellScale;

        // 껍질은 컬링이 꺼져 있어 앞면과 뒷면이 모두 래스터라이즈된다. 가산 블렌딩이라
        // 행성 실루엣 바깥에서는(뒷면이 깊이 테스트를 통과하는 유일한 구간) 같은 광선이
        // 두 번 더해져 테두리만 두 배로 밝아진다. 바깥 법선과 시선 방향이 같으면 뒷면이다.
        if (dot(fragPos - ubo.cameraPos, fragPos - center) > 0.0) discard;

        vec3 rd = normalize(fragPos - ubo.cameraPos);
        vec3 scattered = AtmosphereScattering(ubo.cameraPos, rd, center, planetR, atmoR);

        if (dot(scattered, vec3(1.0)) < 0.0005) discard;
        writeOut(vec4(scattered, 1.0));
        return;
    }
    else if (fragObjectType == 3) { 
        // 축척 보간값. 0 = 기본 축척, 1 = 실제 축척. main.cpp가 customData로 넘긴다.
        float t = clamp(push.customData, 0.0, 1.0);

        vec3 stars = max(texture(skyboxTex, fragTexCube).rgb, 0.0);
        // 은하수 층은 실제 축척으로 갈수록 낮은 밉에서 읽는다. 어두운 별이 확산광으로
        // 뭉개져, 맨눈에 은하수가 보이는 방식과 같아진다(위 SKY_BAND_LOD 주석 참조).
        vec3 band = max(texture(skyBandTex, fragTexCube, SKY_BAND_LOD * t).rgb, 0.0);
        // max로 0에서 자르는 이유: 큐브맵을 구울 때 쓴 란초시 보간에 음의 곁잎이 있어
        // 밝은 별 둘레에 음수가 남을 수 있는데, pow(음수, 비정수)는 GLSL에서 정의되지 않는다.

        // ── 하늘의 두 모습 ──────────────────────────────────────────────────
        // 기본 축척(t=0)은 장노출 사진 같은 하늘이다. hiptyc + milkyway = starmap 이므로
        // 양쪽에 같은 8.0을 곱하면 통합본을 쓰던 예전 출력이 그대로 재현된다.
        //
        // 실제 축척(t=1)은 맨눈으로 본 하늘이다. 상수는 측정으로 정했다 —
        // 별 개수가 전천 9,100개(대기 없는 우주의 맨눈 한계 6.5등급)가 되는 원시값 문턱을
        // 이분법으로 찾고, 그 문턱이 화면 가시 문턱에 걸리도록 감마와 배율을 맞췄다.
        //
        // 층을 나눈 이유가 여기 있다. 통합본에 하나의 곡선만 걸면 별 개수를 맞추는 순간
        // 은하수가 사라진다(띠의 원시값 0.044 < 6.5등급 별의 문턱 0.261). 실제로는 둘 다
        // 보이는데, 띠는 넓어서 눈이 공간적으로 적분해 인지하기 때문이다. 픽셀 하나만 보는
        // 톤 곡선으로는 표현할 수 없는 차이라, 층을 갈라 각자 다른 곡선을 건다.
        float g  = mix(1.0, SKY_STAR_GAMMA, t);
        float k1 = mix(8.0, SKY_STAR_SCALE, t);
        float k2 = mix(8.0, SKY_BAND_SCALE, t);

        vec3 darkSky = pow(stars, vec3(g)) * k1;

        // 은하수는 실제 축척에서 회색 쪽으로 뺀다. 갈색은 장노출 사진의 색이고,
        // 실제로는 눈의 색각 문턱보다 어두워 회백색으로 보인다.
        float bandLuma = dot(band, vec3(0.2126, 0.7152, 0.0722));
        darkSky += mix(band, vec3(bandLuma), SKY_BAND_DESAT * t) * k2;

        float brightness = dot(darkSky, vec3(0.299, 0.587, 0.114));
        float starSeed = fract((darkSky.r * 123.456) + (darkSky.g * 789.012) + (darkSky.b * 345.678) + (fragTexCube.x * 12.34));
        float twinkle = sin(ubo.time * (1.0 + starSeed * 2.0) + starSeed * 100.0) * 0.5 + 0.5;
        // 반짝임은 지구 대기의 굴절로 생기는 현상이라 우주에는 없다(ISS 영상의 별이
        // 미동도 없는 이유). 실제 축척으로 갈수록 뺀다.
        // 문턱은 새 분포에 맞춘 값이다. 예전 0.35는 은하수 띠까지 '별'로 잡아 띠가 깜빡였다.
        float starMask = smoothstep(1.0, 4.0, brightness) * (1.0 - t);

        writeOut(vec4(darkSky * mix(1.0, 0.4 + twinkle * 1.2, starMask), 1.0));
        return;
    }
    // ---------------------------------------------------------
    // ☀️ 태양 본체 (SSS 스타일: 쌀알 무늬 플라즈마 & 강렬한 주연 감광)
    // ---------------------------------------------------------
    else if (fragObjectType == 4) { 
        vec3 texColor = texture(texDiffuse, fragTexCoord).rgb;
        
        // 플라즈마를 조밀하게 쪼개서 쌀알 무늬 표면 구현
        float n1 = fbm(baseNormal * 15.0 + ubo.time * 0.05);
        float n2 = fbm(baseNormal * 30.0 - ubo.time * 0.03);
        float plasma = (n1 + n2) * 0.5;
        
        float grain = smoothstep(0.3, 0.7, plasma);
        
        // SSS의 상징인 극적인 가장자리 어두워짐(Limb Darkening)
        float viewDot = max(dot(viewDir, baseNormal), 0.0);
        float limb = pow(viewDot, 1.5); 
        
        vec3 centerColor = vec3(1.0, 0.95, 0.8); 
        vec3 edgeColor = vec3(0.9, 0.15, 0.0);   
        vec3 sunColor = mix(edgeColor, centerColor, limb);
        
        vec3 finalColor = texColor * sunColor * (0.5 + grain * 1.0);
        writeOut(vec4(finalColor * 2.5, 1.0)); 
        return;
    }
    // ---------------------------------------------------------
    // 🔥 태양 가장자리 불꽃 (팬텀 링 버그 및 깊이 정렬 완벽 해결!)
    // ---------------------------------------------------------
    else if (fragObjectType == 7) { 
        vec3 sunCenter = vec3(push.model[3]);
        // 2.5는 main.cpp의 haloModel 배율과 반드시 같아야 한다(둘 중 하나만 바꾸면 태양이 깨진다).
        float actualSunRadius = length(vec3(push.model[0])) / 2.5;
        float domeRadius = actualSunRadius * 2.5; 
        
        vec3 rayDir = normalize(fragPos - ubo.cameraPos);
        vec3 oc = sunCenter - ubo.cameraPos;
        float camDist = length(oc);
        
        // 돔 내/외부 뒷면 렌더링 방향 처리
        if (camDist > domeRadius) {
            if (dot(fragNormal, rayDir) > 0.0) discard; 
        } else {
            if (dot(fragNormal, rayDir) < 0.0) discard; 
        }
        
        float t = dot(oc, rayDir); 
        
        // 🚀 [핵심 1: 팬텀 링(뒤통수 렌더링) 차단]
        // 빛이 태양을 향하지 않고(t < 0) 카메라가 태양 밖에 있다면, 
        // 등 뒤에서 생기는 유령 홍염 띠를 즉시 삭제합니다!
        if (t < 0.0 && camDist > actualSunRadius * 1.5) discard;
        
        float distToRay = length(cross(rayDir, oc)); 
        float r = distToRay / actualSunRadius; 
        
        if (r < 0.95) discard;

        // 바깥쪽 조기 탈출. 홍염은 flames = smoothstep(flameRadius, 0.98, r)로 만들어지는데
        // flameRadius는 아무리 커도 FLAME_R_MAX를 넘지 못한다(아래 상한 유도 참조).
        // 즉 r이 그보다 크면 alpha가 정확히 0이라 어차피 아래에서 discard되는 픽셀인데,
        // 그 전에 fbm을 6번(노이즈 24회) 계산하고 있었다. 돔 반지름이 2.5라 이 바깥 껍질이
        // 돔 면적의 절반을 넘는다.
        // (이 한 줄이 태양 패스를 4.3ms -> 2.0ms로 줄였다. 돔 메시 자체를 줄이는 건
        //  여기서 이미 싸게 버려지므로 추가 이득이 없었다 — 측정으로 확인.)
        //
        // 예전에는 여유를 둬 1.6으로 잘랐는데, 1.48~1.6 껍질은 알파가 반드시 0인데도
        // fbm 6번을 다 계산하고 버려졌다. 이 껍질이 현재 셰이딩되는 면적의 22%다.
        if (r > FLAME_R_MAX) discard;

        // 🚀 [핵심 2: 완벽한 곡면 Z-Depth 변조]
        // 카메라를 돌려도 깊이가 깨지지 않도록, 픽셀마다 구체 표면까지의 거리를 계산합니다.
        float trueDepth = t; // 기본적으로 가장 가까운 평면 거리 사용
        if (distToRay < actualSunRadius) {
            // 태양 본체 앞을 지나는 픽셀은 태양의 곡률을 따라 둥글게 튀어나오게 깊이를 깎아줍니다.
            float offset = sqrt(pow(actualSunRadius, 2.0) - pow(distToRay, 2.0));
            trueDepth = t - offset * 0.9; // 본체보다 아주 살짝 앞(0.9)에 위치
        }
        
        if (trueDepth < 0.1) trueDepth = 0.1; // 화면을 너무 파고들지 않도록 안전장치
        
        // 계산된 완벽한 곡면 깊이를 Z-Buffer에 강제로 덮어씌움
        vec3 depthPos = ubo.cameraPos + rayDir * trueDepth;
        vec4 clipPos = ubo.proj * ubo.view * vec4(depthPos, 1.0);
        gl_FragDepth = clipPos.z / clipPos.w;
        
        // ====================================================================
        // 아담하고 거친 상시 카오스 플레어 (질문자님 맞춤형 세팅)
        // ====================================================================
        vec3 closestPoint = ubo.cameraPos + rayDir * t; 
        vec3 localDir = normalize(closestPoint - sunCenter); 
        
        vec3 volumePos = localDir * (r * 15.0); 
        vec3 flowOffset = vec3(ubo.time * 1.0, ubo.time * 0.5, -ubo.time * 0.8); 
        
        vec3 warpOffset = vec3(
            fbm(volumePos + vec3(0.0, ubo.time * 0.2, 0.0)),
            fbm(volumePos + vec3(5.2, -ubo.time * 0.3, 1.3)),
            fbm(volumePos + vec3(-2.4, 0.0, ubo.time * 0.4))
        ) * 2.5; 
        
        float n1 = fbm((volumePos + warpOffset) * 1.2 - flowOffset * 1.5);
        // n1만 알아도 chaoticNoise의 상한은 정해진다(n2 <= FBM_MAX). 그 상한으로도
        // 불꽃이 여기까지 못 오면 남은 fbm 두 번은 계산할 필요가 없다.
        if (r > FLAME_R(SHAPE(n1 * 0.7 + FBM_MAX * 0.3))) discard;

        float n2 = fbm((volumePos + warpOffset) * 2.5 - flowOffset * 2.0);
        float chaoticNoise = (n1 * 0.7 + n2 * 0.3);
        // 같은 논리를 한 번 더. 이제 fineDetail만 남았다.
        if (r > FLAME_R(SHAPE(chaoticNoise))) discard;

        // 가느다란 필라멘트 디테일: 더 높은 주파수 노이즈로 불꽃 가장자리를 잘게 쪼갠다.
        float fineDetail = fbm((volumePos + warpOffset) * 5.0 - flowOffset * 3.0);

        float flamesShape = pow(chaoticNoise, 3.0) * 3.0;
        flamesShape *= (0.7 + 0.6 * fineDetail);   // 필라멘트 변조
        float flameRadius = 1.01 + flamesShape * 0.15;

        float flames = smoothstep(flameRadius, 0.98, r);
        float edgeFade = smoothstep(2.0, 1.8, r);

        float alpha = flames * edgeFade;
        if (alpha < 0.005) discard;

        vec3 colorInner = vec3(1.0, 0.8, 0.1);
        vec3 colorOuter = vec3(0.9, 0.1, 0.0);
        vec3 finalColor = mix(colorOuter, colorInner, smoothstep(1.2, 0.95, r));

        finalColor += vec3(1.0, 0.45, 0.05) * flamesShape * smoothstep(1.15, 0.95, r);

        // 가장 강한 불꽃 끝을 백열(하양)로 처리해 온도차를 살린다.
        float hotTip = smoothstep(0.7, 1.6, flamesShape);
        finalColor += vec3(1.0, 0.95, 0.75) * hotTip * smoothstep(1.1, 0.96, r);

        writeOut(vec4(finalColor * 2.5, min(alpha, 1.0)));
        return;
    }
    else if (fragObjectType == 0 || fragObjectType == 9) { 
        vec2 uv = fragTexCoord;

        if (fragObjectType == 9) {
            // 실제 목성 대기는 흐르지만, 정적 텍스처에 시간비례 차등 스크롤(time*speed)을 주면
            // 위도 간 어긋남이 무한 누적되어 무늬가 찢어진다. sin으로 좁은 범위를 왕복만 시켜
            // 누적 없이 은은한 대기 일렁임만 남긴다(강체 자전은 모델 행렬이 담당한다).
            float distFromEquator = abs(uv.y - 0.5) * 2.0;
            float sway = sin(ubo.time * 0.05 + distFromEquator * 6.2831) * 0.004;
            uv.x += sway;
        }

        vec3 preciseNormal = calculateNormal(uv, baseNormal);
        
        float surfaceNdotL = dot(preciseNormal, lightDir);
        float sphereNdotL = dot(baseNormal, lightDir); 
        float NdotV = max(dot(baseNormal, viewDir), 0.001);

        float shadow = SunVisibility(fragPos);

        // 가스 행성은 고리 그림자를 함께 받는다(고리 반지름은 generateRing(1.2, 2.2)와 일치).
        // 토성 외에는 texClouds가 알파 0 더미라 자동으로 1.0이 반환된다.
        if (fragObjectType == 9) {
            shadow *= RingShadow(fragPos, ubo.sunPos, push.model, 1.2, 2.2);
        }

        // 위성이 본체에 드리우는 그림자(예: 목성 위의 이오)는 SunVisibility가 계 전체를 한 번에 처리한다.
        
        // =========================================================
        // 🚀 1. 지표면 노을 틴팅 (Terrain Sunset Tint)
        // 태양빛이 비스듬히 들어오는 경계선 부근에서, 공기가 아닌 '지면(땅)'이 붉은 노을빛을 받도록 조명 색상을 바꿉니다.
        // =========================================================
        float terrainSunsetMask = smoothstep(0.2, -0.1, surfaceNdotL); 
        vec3 sunlightColor = mix(vec3(1.0, 1.0, 1.0), vec3(1.0, 0.35, 0.1), terrainSunsetMask); // 흰빛 -> 주황빛
        
        float diff = max(surfaceNdotL, 0.0) * shadow; 
        // 바다의 태양 반사. 지수는 DSCOVR/EPIC 사진에서 재서 맞췄다.
        // 실제 사진의 바다 밝기는 태양 직하점에서 79, 13도에서 61, 22도에서 55(최저)로
        // 반사가 13도 안쪽에서 끝난다. 지수 16은 절반 밝기 지점이 33도라 2.5배 넓어서
        // 원반의 넓은 면적이 뿌옇게 떴다. 180이면 그 지점이 10도로 실측에 맞는다.
        float spec = pow(max(dot(preciseNormal, normalize(lightDir + viewDir)), 0.0), 180.0);
        // 세기는 0.15에서 크게 낮췄다. 실제 태양 반사는 파도에 잘게 부서지고 구름에 가려
        // 얼룩덜룩한데, 매끈한 구면에 퐁 반사를 얹으면 그 대신 큰 흰 원반이 생겨 눈에 거슬린다.
        // 좁힌 로브에 약한 세기를 조합해 존재만 남긴다.
        vec3 specular = vec3(0.035) * spec * texture(texSpecular, uv).r * shadow;
        
        float dayMix = smoothstep(-0.2, 0.2, surfaceNdotL);
        float eclipseMix = smoothstep(0.0, 1.0, shadow); 
        float finalDayMix = dayMix * eclipseMix;
        
        vec3 baseTexColor = ApplyDetail(texture(texDiffuse, uv).rgb, baseNormal,
                                       DetailFactor(uv, texDiffuse));
        
        // 지면에 닿는 조명(diff)에 방금 만든 노을빛(sunlightColor)을 곱해줍니다!
        vec3 finalColor = baseTexColor * diff * sunlightColor + specular;
        vec3 nightLights = pow(texture(texNight, uv).rgb, vec3(3.0)) * (1.0 - finalDayMix);

        // 예전에는 여기서 프레넬로 파란 띠를 칠해 대기를 흉내 냈다. 이제는 타입 11의
        // 대기 껍질이 실제 산란을 적분해 그리므로, 표면은 표면만 담당한다.
        writeOut(vec4(finalColor + nightLights, 1.0));
        return;
    }
    else if (fragObjectType == 1) { // 달 · 위성 · 왜소행성
        // normal 맵을 반영해 크레이터 요철이 명암 경계에서 드러나게 한다.
        // normal 맵이 없는 천체는 더미 평면 법선(0,0,1)이 바인딩되어 있어 기존 외형이 그대로 유지된다.
        vec3 preciseNormal = calculateNormal(fragTexCoord, baseNormal);
        // 모행성이나 다른 위성에 가려지는 경우(예: 목성 그림자로 들어가는 이오)를 함께 처리한다.
        float shadow = SunVisibility(fragPos);
        float diff = max(dot(preciseNormal, lightDir), 0.0) * shadow;
        vec3 moonAlbedo = ApplyDetail(texture(texDiffuse, fragTexCoord).rgb, baseNormal,
                                     DetailFactor(fragTexCoord, texDiffuse));

        // 이오의 화산 열점은 용암이 식으며 내는 열복사라 햇빛과 무관하게 스스로 빛난다.
        // 낮에는 반사광에 묻혀 안 보이므로 밤면에서만 드러나게 한다(지구 야경과 같은 방식).
        // 열점 마스크가 없는 천체는 더미 검정이 바인딩되어 이 항이 0이 된다.
        float glowMask = texture(texNight, fragTexCoord).r;
        float nightSide = 1.0 - smoothstep(-0.05, 0.25, dot(baseNormal, lightDir) * shadow);
        vec3 glow = glowMask * nightSide * vec3(1.0, 0.30, 0.06) * 0.8;

        writeOut(vec4(moonAlbedo * diff + glow, 1.0));
        return;
    }
    else if (fragObjectType == 2) { // 구름
        float shadow = SunVisibility(fragPos);
        float diff = max(dot(baseNormal, lightDir), 0.0) * shadow;
        writeOut(vec4(texture(texClouds, fragTexCoord).rgb * diff, texture(texClouds, fragTexCoord).r));
        return;
    }
    else if (fragObjectType == 5) { // 토성 고리
        vec3 planetCenter = vec3(push.model[3]);
        float planetRadius = length(vec3(push.model[0])); 
        vec3 rayDirToSun = normalize(ubo.sunPos - fragPos);
        float projLength = dot(planetCenter - fragPos, rayDirToSun); 
        
        float shadow = 1.0;
        if (projLength > 0.0 && projLength < length(ubo.sunPos - fragPos)) {
            float distToRay = length(cross(rayDirToSun, planetCenter - fragPos));
            shadow = smoothstep(planetRadius * 0.95, planetRadius * 1.0, distToRay);
        }
        vec4 ringColor = texture(texDiffuse, fragTexCoord);
        writeOut(vec4(ringColor.rgb * max(abs(dot(baseNormal, normalize(ubo.sunPos - fragPos))), 0.05) * shadow, ringColor.a));
        return;
    }
    else if (fragObjectType == 6) { // 궤도선
        outColor = vec4(0.8, 1.0, 0.0, 0.5);
        outBrightColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    else if (fragObjectType == 12) { // 별자리 선
        outColor = vec4(fragColor, push.customData);
        outBrightColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    // ---------------------------------------------------------
    // 🪨 소행성대 및 카이퍼 벨트 (오브젝트 타입 8) - 리얼 바위 셰이더
    // ---------------------------------------------------------
    else if (fragObjectType == 8) {
        
        // 🚀 [해결 1] UV 좌표 즉석 생성 (Spherical Mapping)
        vec3 localDir = normalize(fragTexCube);
        float u = 0.5 + atan(localDir.z, localDir.x) / (2.0 * 3.14159265);
        float v = 0.5 - asin(localDir.y) / 3.14159265;
        vec3 albedo = texture(texDiffuse, vec2(u, v)).rgb;
        
        // =========================================================
        // 🚀 [수정됨] 팽창 스케일 역산 적용
        // =========================================================
        // 모델 매트릭스의 X축 길이를 통해 현재 몇 배로 팽창했는지 배율을 구합니다.
        float currentScale = length(push.model[0].xyz);
        
        // 현재 픽셀 거리에서 팽창 배율을 나누어 '원래 거리'로 되돌립니다.
        float originalDist = length(fragPos - ubo.sunPos) / currentScale; 
        
        // 팽창된 거리가 아닌 보정된 원래 거리(originalDist)를 기준으로 판별합니다!
        if (originalDist > 30.0) {
            // 🧊 카이퍼 벨트 (해왕성 밖): 푸르스름한 얼음빛 틴트 적용
            albedo = albedo * vec3(0.5, 0.75, 1.3) + vec3(0.0, 0.05, 0.1);
        } else {
            // 🪨 소행성대 (화성~목성): 건조하고 녹슨 바위 톤 적용
            albedo = albedo * vec3(1.2, 0.9, 0.7);
        }
        // =========================================================

        // 🚀 [해결 2] 점토 질감 타파! 편미분을 이용한 '플랫 셰이딩(Flat Shading)'
        vec3 dx = dFdx(fragPos);
        vec3 dy = dFdy(fragPos);
        vec3 sharpNormal = normalize(cross(dx, dy));
        
        // (이하 코드 동일)
        vec3 lightDir = normalize(ubo.sunPos - fragPos);
        float diff = max(dot(sharpNormal, lightDir), 0.05); 
        
        // 소행성은 구가 아니라 '가리는 쪽'은 될 수 없지만, '가려지는 쪽'은 될 수 있다.
        // (소행성끼리 드리우는 그림자는 화면에서 사실상 보이지 않아 잃는 게 없다)
        float shadow = SunDiscOcclusion(fragPos);

        vec3 finalColor = albedo * diff * (1.0 - shadow * 0.8);
        finalColor = pow(finalColor, vec3(0.85)); 
        
        writeOut(vec4(finalColor, 1.0));
        return;
    }
    // 타입 10은 멀리 축소하면 떠오르던 은하 원반이었다. 하늘 큐브맵은 은하 안에서 밖을
    // 본 모습이고 원반은 은하를 밖에서 본 모습이라, 둘이 함께 보이면 관측자가 은하 안에도
    // 밖에도 있게 된다. 원반 쪽을 없앴다.
}