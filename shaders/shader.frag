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
layout(binding = 6) uniform samplerCube skyboxTex;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outBrightColor; 

layout(push_constant) uniform PushConstants { mat4 model; int objectType; float customData;} push;

float hash(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.x + p.y) * p.z); }
float noise3D(vec3 x) { vec3 p = floor(x); vec3 f = fract(x); f = f * f * (3.0 - 2.0 * f); return mix(mix(mix(hash(p+vec3(0,0,0)),hash(p+vec3(1,0,0)),f.x),mix(hash(p+vec3(0,1,0)),hash(p+vec3(1,1,0)),f.x),f.y),mix(mix(hash(p+vec3(0,0,1)),hash(p+vec3(1,0,1)),f.x),mix(hash(p+vec3(0,1,1)),hash(p+vec3(1,1,1)),f.x),f.y),f.z); }
float fbm(vec3 x) { float v = 0.0; float a = 0.5; vec3 shift = vec3(100.0); for(int i=0; i<4; ++i){ v+=a*noise3D(x); x=x*2.0+shift; a*=0.5; } return v; }

vec3 calculateNormal(vec2 uv, vec3 baseNormal) {
    vec3 tangentNormal = texture(texNormalDisp, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= 5.0; tangentNormal = normalize(tangentNormal);
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
const float kAtmoDensity  = 1.4;                    // 전체 광학 두께(눈으로 맞춘 값)
const float kAtmoBright   = 0.8;                    // 산란광 밝기.
// 22.0으로 시작했다가 화면이 통째로 흰 공이 됐다. 시선이 대기를 비스듬히 길게 지나는
// 가장자리에서는 광학 경로가 중심의 18배까지 길어져, 중심을 기준으로 맞추면 림이
// 15를 넘겨 톤매퍼와 블룸을 동시에 포화시킨다. 림이 1.4 근처에 오도록 잡은 값이다.
// 밀도를 대신 올리면 소광이 세져 림이 하얗게 바래므로, 파란 대기를 원하면 밝기로 잡아야 한다.
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

    float ds = (tEnd - tStart) / float(VIEW_STEPS);
    vec3  accum = vec3(0.0);
    float viewOptical = 0.0;

    // 일식으로 태양이 가려지면 산란시킬 빛도 줄어든다. 시선 전체에 한 번만 적용해도
    // 충분하다 — 대기 두께에 걸쳐 가림 정도가 눈에 띄게 달라지지는 않는다.
    float sunVis = SunVisibility(ro + rd * ((tStart + tEnd) * 0.5));

    for (int i = 0; i < VIEW_STEPS; ++i) {
        vec3  P = ro + rd * (tStart + (float(i) + 0.5) * ds);
        float h = length(P - center) - planetR;
        float density = exp(-h / scaleHeight) * (ds / planetR);
        viewOptical += density;

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
        for (int j = 0; j < LIGHT_STEPS; ++j) {
            vec3 Q = P + L * ((float(j) + 0.5) * lds);
            float hq = (length(Q - center) - planetR) / scaleHeight;
            // 지면 아래에서 지수가 폭주해 inf/NaN이 되지 않도록 상한을 둔다.
            lightOptical += exp(-clamp(hq, -12.0, 60.0)) * (lds / planetR);
        }

        // 태양 -> 이 지점 -> 눈까지 오는 동안 흩어져 사라진 만큼을 감쇠시킨다.
        // 파란빛이 먼저 사라지므로, 빛이 대기를 길게 지나온 명암 경계에서는 붉은빛만 남는다.
        vec3 tau = min(kRayleigh * kAtmoDensity * (viewOptical + lightOptical), vec3(80.0));
        accum += density * exp(-tau);
    }

    // 레일리 위상함수. 전방/후방 산란이 측방보다 강해서 역광일 때 대기가 링처럼 빛난다.
    float mu = dot(rd, normalize(ubo.sunPos - ro));
    float phase = 0.75 * (1.0 + mu * mu);

    return accum * kRayleigh * kAtmoDensity * phase * kAtmoBright * sunVis;
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
        vec3 rawSky = texture(skyboxTex, fragTexCube).rgb;
        
        // 🚀 [진짜 해결책] 감마 보정(Gamma Correction) 강제 적용
        // 날것의 EXR 데이터에 2.2 제곱을 가해, 중간톤의 탁한 자주색을 확 가라앉히고 우주의 깊고 어두운 심연(블랙)을 복구합니다.
        vec3 darkSky = pow(rawSky, vec3(2.5)); 
        
        // 전체적인 밝기가 여전히 높다면, 여기서 수치를 곱해 노출을 낮출 수 있습니다. (예: 0.5)
        darkSky *= 2.0; 
        
        float brightness = dot(darkSky, vec3(0.299, 0.587, 0.114));
        float starSeed = fract((darkSky.r * 123.456) + (darkSky.g * 789.012) + (darkSky.b * 345.678) + (fragTexCube.x * 12.34));
        float twinkle = sin(ubo.time * (1.0 + starSeed * 2.0) + starSeed * 100.0) * 0.5 + 0.5;
        float starMask = smoothstep(0.35, 0.8, brightness);
        
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
        // flameRadius는 아무리 커도 1.01 + 3.15*0.15 ≈ 1.48을 넘지 못한다(fbm 합의 상한).
        // 즉 r이 그보다 크면 alpha가 정확히 0이라 어차피 아래에서 discard되는 픽셀인데,
        // 그 전에 fbm을 6번(노이즈 24회) 계산하고 있었다. 돔 반지름이 2.5라 이 바깥 껍질이
        // 돔 면적의 절반을 넘는다. 여유를 둬 1.6으로 자른다 — 화면상 변화는 없다.
        // (이 한 줄이 태양 패스를 4.3ms -> 2.0ms로 줄였다. 돔 메시 자체를 줄이는 건
        //  여기서 이미 싸게 버려지므로 추가 이득이 없었다 — 측정으로 확인.)
        if (r > 1.6) discard;

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
        float n2 = fbm((volumePos + warpOffset) * 2.5 - flowOffset * 2.0);
        float chaoticNoise = (n1 * 0.7 + n2 * 0.3);
        
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
        float spec = pow(max(dot(preciseNormal, normalize(lightDir + viewDir)), 0.0), 16.0);
        vec3 specular = vec3(0.15) * spec * texture(texSpecular, uv).r * shadow;
        
        float dayMix = smoothstep(-0.2, 0.2, surfaceNdotL);
        float eclipseMix = smoothstep(0.0, 1.0, shadow); 
        float finalDayMix = dayMix * eclipseMix;
        
        vec3 baseTexColor = texture(texDiffuse, uv).rgb;
        
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
        writeOut(vec4(texture(texDiffuse, fragTexCoord).rgb * diff, 1.0));
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
    // ---------------------------------------------------------
    // 🌌 우리은하 (오브젝트 타입 10) - 투명 발광 디스크
    // ---------------------------------------------------------
    else if (fragObjectType == 10) {
        vec4 galaxyTex = texture(texDiffuse, fragTexCoord);

        float brightness = max(max(galaxyTex.r, galaxyTex.g), galaxyTex.b);
        float distToCenter = length(fragTexCoord - vec2(0.5));
        float edgeFade = smoothstep(0.48, 0.2, distToCenter);

        // C++에서 계산한 거리 기반 페이드인(0~1). 곡선은 main.cpp에서 smoothstep으로 부드럽게 처리.
        float distanceFade = push.customData;

        float alpha = brightness * edgeFade * distanceFade;
        if (alpha < 0.02) discard;

        vec3 finalColor = galaxyTex.rgb * vec3(0.85, 0.95, 1.3);

        writeOut(vec4(finalColor * 1.5, alpha * 0.8));
        return;
    }
}