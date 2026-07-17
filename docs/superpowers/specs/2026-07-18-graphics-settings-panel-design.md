# 그래픽 설정 창 (Settings Panel) 설계

## 배경 / 목표

지금은 전체화면·궤도선·리얼스케일 토글이 화면에 흩어진 개별 버튼으로 존재한다. 이를 **기어(⚙)
아이콘 하나 + 통합 설정 창**으로 대체하고, 일반 게임의 "그래픽 설정" 수준 항목을 추가한다. 설정은
파일에 저장되어 다음 실행 때 복원된다.

기어 아이콘은 로비·시뮬레이션 양쪽 우측 상단에 항상 표시된다. 기존 개별 버튼(전체화면 등)은 제거한다.

## 설정 항목과 값

### Graphics (항상 사용 가능)

| 항목 | 값/범위 | 기본 | 적용 방식 |
|---|---|---|---|
| Resolution | 1280×720 / 1600×900 / 1920×1080 / 2560×1440 / 3840×2160 | 1920×1080 (파일 없을 때); 로드 시 모니터보다 큰 값은 창모드에서 제외 | 창 크기 변경 + 스왑체인 재생성 |
| Render Scale | 0.5 / 0.75 / 1.0 / 1.5 / 2.0 | 1.0 | 오프스크린/블러 이미지 재생성 |
| Anti-aliasing | Off / 2x / 4x / 8x (기기 미지원 레벨은 목록에서 제외) | 기기 최대(≤8x) | 오프스크린 렌더패스·이미지·오프스크린 파이프라인 재생성 |
| VSync | On / Off | On | present mode 변경 + 스왑체인 재생성 |
| FOV | 30°~90° | 45° | 즉시(다음 프레임 proj 반영) |
| Brightness (Exposure) | 0.3~3.0 | 1.0 | 즉시(post 셰이더 uniform) |
| Frame Cap | 30 / 60 / 120 / 144 / Unlimited | Unlimited | 즉시(메인 루프 제한) |
| Show FPS | On / Off | Off | 즉시(ImGui 오버레이) |
| Fullscreen | On / Off | Off | 기존 전체화면 토글 재사용 |

### View (시뮬레이션에서만 활성, 로비에선 비활성 표시)

| 항목 | 값 | 기본 | 적용 |
|---|---|---|---|
| Orbit Lines | On / Off | Off | 즉시(`showOrbits`) |
| Real Scale | On / Off | Off | 즉시(`scaleLerp` 타이머 목표 전환) |

## 설정 상태 모델

한 곳에 모으는 `GraphicsSettings` 구조체(POD)를 둔다. 각 필드는 위 표의 값을 담고, 런타임 위젯이
이 구조체를 직접 편집한다. 적용은 "변경 감지 → 해당 리소스 재생성" 방식(아래 적용 파이프라인).

```cpp
struct GraphicsSettings {
    int   resolutionIndex = 2;      // 위 해상도 목록 인덱스 (기본 1920x1080)
    float renderScale     = 1.0f;
    int   msaaLevel       = 8;      // 0(off)/2/4/8; 기기 지원으로 상한 클램프
    bool  vsync           = true;
    float fovDegrees      = 45.0f;
    float exposure        = 1.0f;
    int   frameCap        = 0;      // 0 = unlimited, 그 외 목표 fps
    bool  showFps         = false;
    bool  fullscreen      = false;
    bool  orbitLines      = false;
    bool  realScale       = false;
};
```

기존 산재 상태(`showOrbits`, `scaleLerp` 목표, `isFullscreen`, `msaaSamples`, `camera.fov`)는 이
구조체를 단일 소스로 삼아 파생시킨다(중복 상태 제거).

## 저장 / 복원

- 실행 파일 옆 `settings.ini`에 **key=value** 텍스트로 저장한다. JSON 등 외부 라이브러리는 쓰지
  않는다(한 줄씩 파싱하는 최소 코드).
- **로드 시점:** Vulkan 스왑체인/오프스크린을 만들기 **전**에 읽어, 초기 해상도·렌더스케일·MSAA·
  VSync가 파일 값으로 시작되게 한다. 파일이 없으면 위 기본값으로 새로 만든다.
- **저장 시점:** 설정 변경이 확정될 때(위젯 조작 후) 또는 앱 종료 시 기록한다. 저장 실패는 조용히
  무시(권한 문제 등)하되 앱은 계속 동작한다.
- 알 수 없는 키/범위 밖 값은 무시하고 기본값으로 클램프한다(구버전 파일 호환).

## UI

### 기어 아이콘 + 창 열고 닫기

- 우측 상단에 `⚙`(또는 "OPTIONS") 버튼 하나. 로비·시뮬레이션 공통.
- 클릭하면 반투명 설정 패널이 화면 중앙 근처에 열리고, 다시 누르거나 Close/ESC로 닫힌다.
- 패널이 열려 있는 동안에도 뒤 3D 장면은 계속 렌더된다(모달이지만 렌더 정지 아님).

### 레이아웃(개략)

```
┌─ SETTINGS ─────────────────────────────┐
│  GRAPHICS                               │
│   Resolution      [ 1920 x 1080     ▾]  │
│   Render Scale    [ 1.0x            ▾]  │
│   Anti-aliasing   [ 8x MSAA         ▾]  │
│   VSync           [ On              ▾]  │
│   Field of View   [====●======  45° ]   │
│   Brightness      [======●====  1.0 ]   │
│   Frame Cap       [ Unlimited       ▾]  │
│   Show FPS        [ ◻ ]                  │
│   Fullscreen      [ ◻ ]                  │
│                                         │
│  VIEW  (simulation only)                │
│   Orbit Lines     [ ◻ ]   (로비에선 회색) │
│   Real Scale      [ ◻ ]   (로비에선 회색) │
│                                         │
│                             [ Close ]   │
└─────────────────────────────────────────┘
```

- 로비에서는 VIEW 섹션 위젯을 `BeginDisabled()`로 비활성 표시.
- 세부 색감·정확한 위치·기어 아이콘 그래픽은 구현 중 조정(설계 확정 대상 아님).

## 적용 파이프라인 (재생성 처리)

무거운 항목(해상도·렌더스케일·MSAA·VSync)은 GPU 리소스 재생성이 필요하다. 지금 전체화면 토글이
쓰는 것과 같은 방식으로 **프레임 경계에서 안전하게** 처리한다:

- 위젯이 값을 바꾸면 "적용 요청" 플래그를 세운다(예: `pendingRecreate`).
- `onFrameStart`(fence 대기 후, GPU 유휴)에서 `vkDeviceWaitIdle` 후 필요한 재생성을 수행한다:
  - **해상도/VSync** → `recreateSwapChain()` 확장(present mode·창 크기 반영).
  - **렌더스케일** → 오프스크린/블러 이미지·프레임버퍼를 `renderExtent = round(scale × windowExtent)`로
    재생성.
  - **MSAA** → 오프스크린 렌더패스·MSAA 이미지·오프스크린 두 파이프라인(graphics/line) 재생성.
- 가벼운 항목(FOV/노출/프레임캡/토글)은 플래그 없이 다음 프레임에 즉시 반영.

한 프레임에 여러 재생성이 겹쳐도 `vkDeviceWaitIdle` 한 번 뒤 순서대로 처리하면 안전하다.

## 렌더 스케일 아키텍처

지금 오프스크린/블러 이미지는 `swapChainExtent`와 같은 크기다. 렌더 스케일을 도입하면:

- `renderExtent = round(renderScale × swapChainExtent)`를 새로 두고, **오프스크린·블러 패스만** 이
  크기로 렌더한다(뷰포트/시저/이미지 전부 renderExtent).
- **포스트(합성) 패스와 UI는 `swapChainExtent`(창 원본) 그대로**다. 포스트 셰이더가 오프스크린
  텍스처(renderExtent)를 샘플해 창 크기로 축소/확대하므로, 스케일>1이면 수퍼샘플링(가장자리가 더
  깨끗), <1이면 성능 이득. **UI는 항상 창 원본 해상도라 선명함을 유지한다.**
- 레이캐스트/네임태그의 화면 좌표 계산은 창(swapChainExtent) 기준이므로 영향받지 않는다.

## FOV와 기존 스크롤 줌의 관계

현재 `Camera::fov`(기본 45°)를 스크롤 줌이 2°까지 직접 줄인다. 설정의 FOV는 **줌하지 않은 기본
시야각**으로 정의한다:

- `Camera`에 `baseFov`(설정값, 30~90°)를 두고, 실제 렌더에 쓰는 `fov`는 스크롤 줌 계수를 `baseFov`에
  적용해 파생시킨다(줌 인 시 `baseFov`에서 좁혀지고, 줌 아웃의 상한은 `baseFov`).
- 설정에서 FOV를 바꾸면 `baseFov`가 갱신되고, 현재 줌 상태는 그에 맞춰 재계산된다.

## 노출(Brightness) 셰이더 변경

`shaders/post.frag`는 지금 `hdrColor = color + bright` 후 Reinhard 톤매핑을 한다. 톤매핑 **직전에**
`hdrColor *= exposure`를 넣고, `exposure`는 포스트 패스 uniform/푸시상수로 전달한다. 기본 1.0에서
등가(무변화).

## 검증

- 이 저장소엔 자동 테스트가 없다. "테스트" = (a) 빌드 성공, (b) 실행 후 스크린샷 육안 확인, (c) 재생성
  경로에서 Vulkan 오류/크래시 없음.
- 컨트롤러가 확인 가능한 것: 앱 기동, 로비 렌더, 설정 창이 열리는지, 각 위젯 조작 후 크래시 없는지,
  `settings.ini`가 생성·갱신되는지. 해상도/MSAA 변경 전후 스크린샷 비교.
- 마우스 주입이 안 되므로, 설정 창을 **열고 각 항목을 실제로 바꿔 보는 최종 확인은 사용자**가 한다.
- 재생성 경로 검증을 위해 초기 `settings.ini`에 비기본값(예: 렌더스케일 2.0, MSAA 2x, 1600×900)을
  직접 써 넣고 기동해, 시작부터 그 설정으로 뜨는지 컨트롤러가 스크린샷으로 확인할 수 있다(마우스
  없이도 로드 경로 전체를 검증).

## 범위 밖 (명시적으로 하지 않는 것)

- 새 렌더링 효과(SSAO·모션 블러·DoF·볼류메트릭·SSR 등) — 각각 별도 기능.
- 마우스 감도, 키 리매핑, 오디오·언어·자막, 모니터 선택, 창 위치 기억.
- 전용(exclusive) 전체화면 모드 — 기존 보더리스 방식 유지.
- 그림자 품질/비등방 필터링 — 이번 창에는 넣지 않음(원하면 후속).
