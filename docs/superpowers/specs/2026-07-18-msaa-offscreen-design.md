# 오프스크린 패스 MSAA (4x) 설계

## 배경

리얼 스케일에서 왜소행성에 시점을 고정하고 확대하면, 천체 본체는 카메라 상대 렌더링으로 완벽히
고정됐지만(측정 0.00px) 궤도선이 여전히 아주 미세하게 떨린다.

측정으로 원인을 분리했다(`orbit_jitter_test.cpp`): 구운 궤도선 정점을 float32로 저장하든 double로
저장하든 화면 떨림이 **0.5534px로 완전히 동일**하다. 즉 좌표 정밀도가 원인이 아니고, 정밀도를 더
올려도 줄지 않는다. 남은 떨림은 **1픽셀 두께 선의 래스터화 앨리어싱**이다 — 렌더러 전체가
`VK_SAMPLE_COUNT_1_BIT`(안티앨리어싱 없음)라, 장면이 매 프레임 서브픽셀만큼 움직일 때 얇은 선이
픽셀 격자에서 아른거린다. 채워진 구는 픽셀이 많아 안 보이지만 1px 선은 여기에 가장 취약하다.

## 목표

3D 장면을 그리는 오프스크린 패스에 4x MSAA를 적용해 궤도선을 포함한 장면 전체를 안티앨리어싱한다.
궤도선의 서브픽셀 크롤링이 다듬어져 "살짝 떨림"이 사라진다. 구 실루엣·토성 고리 가장자리 등 전반적
이미지 품질도 함께 좋아진다.

## 핵심 아이디어

오프스크린 패스를 멀티샘플로 렌더한 뒤 기존 단일 샘플 이미지로 resolve(다운샘플)한다. 블러·포스트·
UI(ImGui)·그림자 패스는 resolve된 결과를 샘플링하므로 **전부 그대로 둔다.** MSAA는 3D 장면 패스에만
적용된다.

## 샘플 수

8x를 상한으로 둔다(초기 4x에서 상향 — 왜소행성 가장자리의 잔여 앨리어싱을 한 단계 더 줄이기 위해).
`VkPhysicalDeviceProperties.limits.framebufferColorSampleCounts & framebufferDepthSampleCounts`의
교집합에서 지원되는 최대 비트로 8x→4x→2x→1x 순으로 자동 강등한다. 앱 초기화 시 한 번 계산해
멤버에 저장하고, 선택된 값을 stderr로 한 줄 기록한다(어느 레벨이 실제로 적용됐는지 확인용).

**주의:** 남은 떨림은 좌표 정밀도가 아니라 래스터화 가장자리 앨리어싱이므로, MSAA는 이를 점근적으로
줄일 뿐 완전히 0으로 만들지는 못한다(움직이는 가장자리를 유한 샘플로 표현하는 방식의 본질적 한계).
8x가 부족하면 다음 지렛대는 수퍼샘플링(고해상도 렌더 후 축소)이며, 이는 이 설계의 범위 밖이다.

## 변경 대상 (전부 `src/main.cpp`)

| 구성요소 | 변경 |
|---|---|
| 멤버 | `VkSampleCountFlagBits msaaSamples`; MSAA 이미지 3종(색상·bright·깊이)의 image/mem/view 핸들 |
| 샘플 수 질의 | 초기화 시 `msaaSamples` 계산 (물리 디바이스 한계에서 min(4x, 지원최대)) |
| `createOffscreenImages` | 기존 색상·bright(단일 샘플, resolve 대상)는 유지. MSAA 색상·bright·깊이 3개를 `msaaSamples`로 새로 생성. 깊이는 이제 MSAA만 필요(다운스트림에서 안 씀). 프레임버퍼 어태치먼트 3→5개 |
| `createOffscreenResources` | 렌더패스 어태치먼트 3→5개(MSAA 색상·bright·깊이 + resolve 색상·bright). 서브패스에 `pResolveAttachments`(색상 2개) 추가. resolve 어태치먼트 finalLayout = `SHADER_READ_ONLY_OPTIMAL`; MSAA 어태치먼트는 storeOp=DONT_CARE |
| 오프스크린 파이프라인 | `graphicsPipeline`·`linePipeline`이 공유하는 `multisampling` 구조체의 `rasterizationSamples = msaaSamples` (한 곳). 나머지 파이프라인(그림자·블러·포스트)은 다른 패스라 무관 |
| 정리 경로 | `cleanupSwapChain`(리사이즈)와 최종 `cleanupApp` 두 곳에서 MSAA 이미지 3종도 해제 |

## 반드시 지켜야 하는 제약

- **서브패스 내 샘플 수 일치:** MSAA 색상·bright·깊이 어태치먼트의 `samples`는 전부 `msaaSamples`로
  동일해야 한다. resolve 어태치먼트만 1x다.
- **파이프라인·렌더패스 샘플 수 일치:** 오프스크린 파이프라인의 `rasterizationSamples`가 렌더패스
  MSAA 어태치먼트의 `samples`와 반드시 같아야 한다. 다르면 파이프라인 생성이 검증 실패한다.
- **resolve 대상은 여전히 SAMPLED:** 기존 색상·bright 이미지는 `SAMPLED_BIT` 유지(블러/포스트가
  샘플링). MSAA 이미지는 `COLOR_ATTACHMENT_BIT`만 있으면 되고 SAMPLED 불필요.
- **리사이즈 대칭성:** `recreateSwapChain`이 `createOffscreenImages`를 다시 부르므로 MSAA 이미지도
  거기서 재생성되고, `cleanupSwapChain`에서 짝을 맞춰 해제돼야 한다. 누락 시 리사이즈마다 누수.

## 검증

- 이 저장소엔 자동 테스트가 없다. "테스트" = (a) 빌드 성공, (b) 실행 후 스크린샷 육안 확인,
  (c) Vulkan validation layer 경고 없음(파이프라인/렌더패스 샘플 수 불일치는 여기서 잡힌다).
- 컨트롤러가 로비 스크린샷으로 렌더링 정상·크래시 없음을 확인한다. 궤도선 크롤링이 실제로 사라졌는지
  최종 육안 확인은 시뮬레이션 진입·시점 고정이 필요하므로(마우스 주입 불가) 사용자가 수행한다.

## 범위 밖

- UI(ImGui) 안티앨리어싱: 최종 스왑체인 패스는 단일 샘플 유지.
- 블러/블룸 품질, 렌더 해상도(수퍼샘플링), FXAA 등 다른 AA 기법.
- 좌표 정밀도: 카메라 상대 작업으로 이미 해결됨. 손대지 않는다.
