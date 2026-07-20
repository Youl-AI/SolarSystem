#version 450
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) flat in int fragObjectType;
layout(location = 5) in vec3 fragTexCube;
layout(location = 6) in vec4 fragPosLightSpace; 

layout(binding = 0) uniform UniformBufferObject {
    mat4 view; mat4 proj; vec3 cameraPos; float time;
    vec3 sunPos; float sunRadius; vec3 earthPos; float earthRadius; vec3 moonPos; float moonRadius;
    mat4 lightSpaceMatrix;
} ubo;

layout(binding = 1) uniform sampler2D texDiffuse;
layout(binding = 2) uniform sampler2D texNight;
layout(binding = 3) uniform sampler2D texSpecular;
layout(binding = 4) uniform sampler2D texNormalDisp;
layout(binding = 5) uniform sampler2D texClouds;
layout(binding = 6) uniform samplerCube skyboxTex;
// sampler2DShadow: 샘플러에 compareEnable이 켜져 있어, 읽으면 깊이가 아니라 '비교 결과'가 나온다.
// 하드웨어가 비교 후 2x2 보간을 해주므로 탭 하나만으로도 경계가 부드럽다.
layout(binding = 7) uniform sampler2DShadow shadowMap;

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

// 셰도우 맵 가림 정도(0 = 완전히 밝음, 1 = 완전히 가려짐).
// 이 엔진은 GLM_FORCE_DEPTH_ZERO_TO_ONE을 쓰므로 깊이가 Vulkan 규약([0,1])이다.
// xy만 [-1,1] -> [0,1]로 옮기고 z는 그대로 써야 한다. z까지 0.5*z+0.5로 옮기면
// 깊이가 항상 과대평가되어 거의 모든 픽셀이 그늘로 판정된다(예전 소행성 코드의 버그).
float ShadowMapOcclusion(vec4 fragPosLightSpace, vec3 N, vec3 L) {
    vec3 proj = fragPosLightSpace.xyz / fragPosLightSpace.w;
    if (proj.z < 0.0 || proj.z > 1.0) return 0.0;

    vec2 uv = proj.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 0.0;

    // 빛이 비스듬할수록 깊이 기울기가 커져 여드름(acne)이 생기므로 바이어스를 키운다.
    float bias = max(0.0004 * (1.0 - dot(N, L)), 0.00005);

    // 3x3 탭 PCF. 탭마다 하드웨어가 2x2 비교 보간을 해주므로 실질 4x4 커널이 된다.
    // 조기 반환이 있는 비균일 흐름이라 암시적 미분을 못 쓰고 LOD를 명시한다.
    // 탭 간격을 1.5텍셀로 벌려 4096에서도 반그림자 폭이 눈에 보이게 유지한다.
    // (실제로 이오의 그림자는 태양이 점광원이 아니라 가장자리가 수백 km에 걸쳐 흐리다)
    vec2 texel = 1.5 / vec2(textureSize(shadowMap, 0));
    float ref = proj.z - bias;
    float occ = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            occ += textureLod(shadowMap, vec3(uv + vec2(x, y) * texel, ref), 0.0);
    return occ / 9.0;
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

float ProjectShadow(vec3 pixelPos, vec3 sunPos, vec3 occluderPos, float occluderRadius) {
    vec3 lightBeamDir = normalize(occluderPos - sunPos);
    vec3 pixelFromOccluder = pixelPos - occluderPos;
    float distanceBehind = dot(pixelFromOccluder, lightBeamDir);
    if (distanceBehind < 0.0) return 1.0; 
    float distanceFromCenter = length(pixelFromOccluder - lightBeamDir * distanceBehind);
    return smoothstep(occluderRadius * 0.8, occluderRadius, distanceFromCenter);
}

void main() {
    gl_FragDepth = gl_FragCoord.z;

    vec3 baseNormal = normalize(fragNormal);
    vec3 lightDir = normalize(ubo.sunPos - fragPos); 
    vec3 viewDir = normalize(ubo.cameraPos - fragPos);

    if (fragObjectType == 3) { 
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

        float shadow = ProjectShadow(fragPos, ubo.sunPos, ubo.moonPos, ubo.moonRadius);

        // 가스 행성은 고리 그림자를 함께 받는다(고리 반지름은 generateRing(1.2, 2.2)와 일치).
        // 토성 외에는 texClouds가 알파 0 더미라 자동으로 1.0이 반환된다.
        if (fragObjectType == 9) {
            shadow *= RingShadow(fragPos, ubo.sunPos, push.model, 1.2, 2.2);
        }

        // 위성이 본체에 드리우는 그림자(예: 목성 위의 이오 그림자). 해석적 ProjectShadow는
        // 지구의 달 하나만 다루므로, 나머지는 셰도우 맵이 담당한다.
        shadow *= 1.0 - ShadowMapOcclusion(fragPosLightSpace, preciseNormal, lightDir) * 0.9;
        
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

        // =========================================================
        // 🚀 2. 대기 산란 헤일로 (Atmospheric Halo)
        // 발광하는 띠는 온전히 가장자리(fresnel)에서만 맺히게 돌려놓습니다.
        // =========================================================
        float fresnel = pow(1.0 - NdotV, 8.0); 

        float dayMask = smoothstep(-0.1, 0.4, sphereNdotL); 
        float sunsetMask = smoothstep(-0.2, 0.1, sphereNdotL) * smoothstep(0.2, -0.05, sphereNdotL);

        vec3 dayAtmosphere = vec3(0.1, 0.4, 1.0) * dayMask;
        vec3 sunsetAtmosphere = vec3(1.0, 0.3, 0.05) * sunsetMask * 1.5;

        // 대기 발광은 가장자리(fresnel)에만 곱해지므로, 흉한 페인트 띠가 사라집니다.
        vec3 atmosphericGlow = (dayAtmosphere + sunsetAtmosphere) * fresnel * shadow;

        writeOut(vec4(finalColor + nightLights + atmosphericGlow, 1.0));
        return;
    }
    else if (fragObjectType == 1) { // 달 · 위성 · 왜소행성
        // normal 맵을 반영해 크레이터 요철이 명암 경계에서 드러나게 한다.
        // normal 맵이 없는 천체는 더미 평면 법선(0,0,1)이 바인딩되어 있어 기존 외형이 그대로 유지된다.
        vec3 preciseNormal = calculateNormal(fragTexCoord, baseNormal);
        float shadow = ProjectShadow(fragPos, ubo.sunPos, ubo.earthPos, ubo.earthRadius);
        // 모행성이나 다른 위성에 가려지는 경우(예: 목성 그림자로 들어가는 이오)를 셰도우 맵이 잡는다.
        shadow *= 1.0 - ShadowMapOcclusion(fragPosLightSpace, preciseNormal, lightDir) * 0.9;
        float diff = max(dot(preciseNormal, lightDir), 0.0) * shadow;
        writeOut(vec4(texture(texDiffuse, fragTexCoord).rgb * diff, 1.0));
        return;
    }
    else if (fragObjectType == 2) { // 구름
        float shadow = ProjectShadow(fragPos, ubo.sunPos, ubo.moonPos, ubo.moonRadius);
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
        
        // 예전에는 여기서 z까지 0.5*z+0.5로 옮겨 깊이를 과대평가하는 바람에 소행성이
        // 사실상 상시 그늘(밝기 20%)로 그려지고 있었다. 공용 함수가 Vulkan 규약대로 처리한다.
        float shadow = ShadowMapOcclusion(fragPosLightSpace, sharpNormal, lightDir);

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