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
layout(binding = 7) uniform sampler2D shadowMap; 

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
    else if (fragObjectType == 1) { // 달
        float shadow = ProjectShadow(fragPos, ubo.sunPos, ubo.earthPos, ubo.earthRadius);
        float diff = max(dot(baseNormal, lightDir), 0.0) * shadow;
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
        
        float shadow = 0.0;
        vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
        if(projCoords.z > -1.0 && projCoords.z < 1.0) {
            projCoords = projCoords * 0.5 + 0.5;
            float closestDepth = texture(shadowMap, projCoords.xy).r; 
            float currentDepth = projCoords.z;
            float bias = max(0.005 * (1.0 - dot(sharpNormal, lightDir)), 0.001);
            shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
        }
        
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