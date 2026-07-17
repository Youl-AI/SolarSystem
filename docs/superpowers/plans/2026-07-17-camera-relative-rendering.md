# Camera-Relative Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the float32 precision jitter (30.51 px/frame) and faceting (2-4% radius spread) seen when zooming into real-scale dwarf planets, by moving rendering from a world-origin frame to a camera-relative frame.

**Architecture:** All rendering coordinates shift from being relative to the world origin (the sun) to being relative to `camera.target`. Body positions and the camera target are widened to `double` so the single subtraction `worldPos - cameraTarget` is exact; its small result is handed to the GPU as `float`, so per-vertex math happens near zero where float32 has precision to spare. The switch is staged: three no-op preparation tasks make the change mechanical and reviewable, then a 2-line "flip" activates the new frame.

**Tech Stack:** C++17, Vulkan 1.2, GLFW 3.3.8, Dear ImGui 1.90.4, GLM, CMake/MSVC on Windows.

## Global Constraints

- Design reference: `docs/superpowers/specs/2026-07-17-camera-relative-rendering-design.md`
- No automated test suite exists. "Testing" means: (a) `cmake --build build --config Debug` succeeds, (b) the app launches and a screenshot shows correct rendering, (c) the precision harness (Task 7) reports the target numbers.
- **`cmake` is NOT on PATH.** Use the full path:
  `CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"` then `"$CMAKE" --build build --config Debug`
  Run from the repo root. `build/` is already configured (generator "Visual Studio 18 2026", x64) — do NOT re-run `cmake -S . -B build`.
- The executable is `build/Debug/VulkanApp.exe` and must run with the repo root as its working directory (it loads `shaders/*.spv` and `textures/*` via relative paths).
- **Shaders must be recompiled after editing `.frag`/`.vert`.** The app loads pre-compiled `shaders/*.spv`. Recompile with:
  `"/c/VulkanSDK/1.4.350.0/Bin/glslc.exe" shaders/shader.frag -o shaders/frag.spv` (and equivalently `shader.vert -> vert.spv`).
  The `.spv` files are gitignored; do not try to commit them.
- **Do NOT attempt to click, drive, or screenshot the app from a subagent.** Synthetic mouse input does not reach the app in this environment (verified: PostMessage/mouse_event/SendInput/forced-focus all fail). Build verification is your job; visual verification is the controller's. Stop at a clean build and report.
- Tasks 1-4 must be **exact no-ops**: the app must render identically. Task 5 is the only task that changes rendering behavior.
- Do not change the real-scale distance/radius values, and do not restructure code beyond what a task specifies.

---

## Task 1: Make the implicit "world origin == sun" assumptions explicit

Three places compute a sun-relative quantity by exploiting the fact that the sun sits at the world origin. Each is a silent landmine once the frame moves. In the current absolute frame `sunPos == vec3(0)`, so writing them explicitly is a **no-op today** and makes Task 5 safe.

**Files:**
- Modify: `shaders/shader.frag` (2 lines)
- Modify: `src/main.cpp` (1 line)

**Interfaces:**
- Produces: nothing new. Behaviour identical.

- [ ] **Step 1: Saturn's ring — derive the sun direction from `ubo.sunPos`, not from the origin**

In `shaders/shader.frag`, find this line (inside the `fragObjectType == 5` branch):

```glsl
        writeOut(vec4(ringColor.rgb * max(abs(dot(baseNormal, normalize(-fragPos))), 0.05) * shadow, ringColor.a));
```

Replace with:

```glsl
        writeOut(vec4(ringColor.rgb * max(abs(dot(baseNormal, normalize(ubo.sunPos - fragPos))), 0.05) * shadow, ringColor.a));
```

(`normalize(-fragPos)` is the direction from the fragment to the world origin. It only means "toward the sun" because the sun is at the origin.)

- [ ] **Step 2: Asteroid belt tint — measure distance from the sun, not from the origin**

In `shaders/shader.frag`, find this line (inside the `fragObjectType == 8` branch):

```glsl
        float originalDist = length(fragPos) / currentScale; 
```

Replace with:

```glsl
        float originalDist = length(fragPos - ubo.sunPos) / currentScale; 
```

- [ ] **Step 3: Shadow focus — test the locked target directly instead of testing its distance from the origin**

In `src/main.cpp`, find:

```cpp
        glm::vec3 shadowFocusPos = nextTarget;
        if (glm::length(shadowFocusPos) < 0.1f) shadowFocusPos = planets[2].currentPosition; 
```

Replace with:

```cpp
        // 태양(원점)에 고정된 상태면 태양 자신을 그림자 초점으로 삼을 수 없으므로 지구로 대체한다.
        glm::vec3 shadowFocusPos = nextTarget;
        if (lockedTargetType == 0) shadowFocusPos = planets[2].currentPosition;
```

- [ ] **Step 4: Recompile the shaders**

Run:
```bash
"/c/VulkanSDK/1.4.350.0/Bin/glslc.exe" shaders/shader.frag -o shaders/frag.spv
```
Expected: no output, exit code 0.

- [ ] **Step 5: Build**

Run: `"$CMAKE" --build build --config Debug`
Expected: succeeds, `build/Debug/VulkanApp.exe` produced, no new warnings.

- [ ] **Step 6: Commit**

```bash
git add shaders/shader.frag src/main.cpp
git commit -m "Derive sun-relative quantities from sunPos instead of the world origin

No-op today (the sun is at the origin), but these three spots silently
assume it, and camera-relative rendering is about to move the origin."
```

---

## Task 2: Widen the camera target to double

`Camera::target` is an accumulator at ~46000 magnitude; in float32 its ULP is 0.0039, which is 4% of Eris's radius. It must be double so that `worldPos - target` is exact.

**Files:**
- Modify: `src/Camera.hpp`
- Modify: `src/main.cpp` (call sites)

**Interfaces:**
- Produces: `glm::dvec3 Camera::target`, `glm::dvec3 Camera::lastNewTarget`
- Produces: `void Camera::smoothFollow(glm::dvec3 newTarget, float deltaTime, bool targetChanged = false)`
- Produces: `glm::dvec3 Camera::getEyeWorld() const` — the absolute eye position, for callers that still need it
- `Camera::pos` stays `glm::vec3` (it is the small orbit offset, ~0.5 units — float is plenty)

- [ ] **Step 1: Widen the members in `src/Camera.hpp`**

Find:

```cpp
    glm::vec3 target = glm::vec3(5.0f, 0.0f, 0.0f);
    glm::vec3 pos = glm::vec3(0.0f, 0.0f, 1.0f); 
```

Replace with:

```cpp
    // target은 리얼 스케일에서 46000 유닛까지 커지는 누적값이다. float32면 그 지점의 눈금이
    // 0.0039라 왜소행성 반지름(0.09)의 4%나 되므로, 상대 좌표 뺄셈이 정확하도록 double로 둔다.
    glm::dvec3 target = glm::dvec3(5.0, 0.0, 0.0);
    glm::vec3 pos = glm::vec3(0.0f, 0.0f, 1.0f); // 타겟 기준 궤도 오프셋이라 항상 작다
```

Find:

```cpp
    glm::vec3 lastNewTarget = glm::vec3(5.0f, 0.0f, 0.0f);
```

Replace with:

```cpp
    glm::dvec3 lastNewTarget = glm::dvec3(5.0, 0.0, 0.0);
```

- [ ] **Step 2: Widen `smoothFollow` in `src/Camera.hpp`**

Find:

```cpp
    void smoothFollow(glm::vec3 newTarget, float deltaTime, bool targetChanged = false)
    {
        if (targetChanged) {
            lastNewTarget = newTarget;
        } else {
            // 공전 중인 천체의 이번 프레임 이동량을 그대로 상쇄해 카메라를 붙여 놓는다.
            // (이게 없으면 보간만으로는 움직이는 천체를 영원히 따라잡지 못하고 속도/5 만큼 뒤처진다)
            glm::vec3 targetVelocity = newTarget - lastNewTarget;
            lastNewTarget = newTarget;
            target += targetVelocity;
        }

        float lerpFactor = 5.0f * deltaTime;
        target = glm::mix(target, newTarget, std::clamp(lerpFactor, 0.0f, 1.0f));
    }
```

Replace with:

```cpp
    void smoothFollow(glm::dvec3 newTarget, float deltaTime, bool targetChanged = false)
    {
        if (targetChanged) {
            lastNewTarget = newTarget;
        } else {
            // 공전 중인 천체의 이번 프레임 이동량을 그대로 상쇄해 카메라를 붙여 놓는다.
            // (이게 없으면 보간만으로는 움직이는 천체를 영원히 따라잡지 못하고 속도/5 만큼 뒤처진다)
            glm::dvec3 targetVelocity = newTarget - lastNewTarget;
            lastNewTarget = newTarget;
            target += targetVelocity;
        }

        double lerpFactor = 5.0 * (double)deltaTime;
        target = glm::mix(target, newTarget, std::clamp(lerpFactor, 0.0, 1.0));
    }
```

- [ ] **Step 3: Add `getEyeWorld()` and keep `getViewMatrix()` in the absolute frame for now**

Find:

```cpp
    glm::mat4 getViewMatrix() const
    {
        // =========================================================
        // 🚀 [NEW] 기존의 가변 up 벡터를 지우고, 절대 고정된 WORLD_UP을 사용합니다!
        // =========================================================
        return glm::lookAt(target + pos, target, WORLD_UP);
    }
```

Replace with:

```cpp
    // 카메라 눈의 절대 월드 좌표.
    glm::dvec3 getEyeWorld() const { return target + glm::dvec3(pos); }

    glm::mat4 getViewMatrix() const
    {
        // =========================================================
        // 🚀 [NEW] 기존의 가변 up 벡터를 지우고, 절대 고정된 WORLD_UP을 사용합니다!
        // =========================================================
        return glm::lookAt(glm::vec3(target) + pos, glm::vec3(target), WORLD_UP);
    }
```

- [ ] **Step 4: Fix the call sites in `src/main.cpp`**

Find:

```cpp
            glm::vec3 rayOrigin = camera.target + camera.pos;
```

Replace with:

```cpp
            glm::vec3 rayOrigin = glm::vec3(camera.getEyeWorld());
```

Find:

```cpp
        static bool isFirstFrame = true;
        if (isFirstFrame) {
            camera.target = nextTarget;
            camera.lastNewTarget = nextTarget;
```

Replace with:

```cpp
        static bool isFirstFrame = true;
        if (isFirstFrame) {
            camera.target = glm::dvec3(nextTarget);
            camera.lastNewTarget = glm::dvec3(nextTarget);
```

Find:

```cpp
        camera.smoothFollow(nextTarget, deltaTime, targetChanged);
```

Replace with:

```cpp
        camera.smoothFollow(glm::dvec3(nextTarget), deltaTime, targetChanged);
```

Find:

```cpp
            camera.target = glm::mix(camera.target, moonPos, ease);
```

Replace with:

```cpp
            camera.target = glm::mix(camera.target, glm::dvec3(moonPos), (double)ease);
```

Find:

```cpp
        ubo.cameraPos = camera.target + camera.pos; 
```

Replace with:

```cpp
        ubo.cameraPos = glm::vec3(camera.getEyeWorld());
```

Find (in `updateUniformBuffer`, the camera-tracking block):

```cpp
        glm::vec3 nextTarget = camera.target;
```

Replace with:

```cpp
        glm::vec3 nextTarget = glm::vec3(camera.target);
```

- [ ] **Step 5: Build**

Run: `"$CMAKE" --build build --config Debug`
Expected: succeeds with no errors. If the compiler reports a `glm::vec3`/`glm::dvec3` mismatch at a call site not listed above, that call site also needs an explicit `glm::vec3(...)` or `glm::dvec3(...)` conversion — report it in your notes.

- [ ] **Step 6: Commit**

```bash
git add src/Camera.hpp src/main.cpp
git commit -m "Widen Camera::target to double

It is a ~46000-magnitude accumulator; float32 quantises it to a 0.0039 grid,
4% of a dwarf planet's radius. Rendering still uses the absolute frame."
```

---

## Task 3: Widen body positions to double and split position computation from model-matrix construction

Two changes that must land together, because the second is what makes Task 5 possible.

**Positions to double:** so `worldPos - cameraTarget` is exact.

**Split the passes:** `updateUniformBuffer` currently builds each body's model matrix in the *same* loop that computes its position — which runs *before* the camera update. Once model matrices are camera-relative (Task 5) they must be built from the *current* frame's `camera.target`, or the model matrix and the view matrix would disagree by a whole frame of camera motion (5.9 units for Eris — the body would render far off-centre). So position computation moves to a first pass, and model-matrix construction to a second pass after the camera update.

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: `glm::dvec3 Planet::currentPosition`
- Produces: `void SolarSystemApp::buildBodyModelMatrices(float easeScale, float slowTime, float time)` — second pass; fills `currentModelMat` / `cloudModelMat` for the sun, every planet, and the moon. Called after the camera update.
- Consumes: `Camera::getEyeWorld()` (Task 2)

- [ ] **Step 1: Widen `Planet::currentPosition` in `src/main.cpp`**

Find:

```cpp
    glm::mat4 currentModelMat; glm::vec3 currentPosition; glm::mat4 cloudModelMat;
```

Replace with:

```cpp
    glm::mat4 currentModelMat; glm::dvec3 currentPosition; glm::mat4 cloudModelMat;
```

- [ ] **Step 2: Make the orbital math double and drop model-matrix construction from the position pass**

In `updateUniformBuffer`, find the whole planet loop, from:

```cpp
        // 🚀 2. 행성 및 위성 크기/거리 믹스 적용
        for (int i = 0; i < planets.size(); i++) {
```

through its closing brace, ending with:

```cpp
                moon.currentModelMat = moonBase * moonRot * glm::scale(glm::mat4(1.0f), glm::vec3(curMoonRadius));
                moon.currentPosition = glm::vec3(moonBase[3]);
            }
        }
```

Replace that entire block with:

```cpp
        // 🚀 2. 행성 및 위성의 '위치'만 double로 계산한다.
        //    모델 행렬은 카메라 업데이트가 끝난 뒤 buildBodyModelMatrices()에서 만든다.
        //    (카메라 상대 좌표를 쓰려면 모델 행렬이 이번 프레임의 camera.target을 봐야 하기 때문)
        for (int i = 0; i < planets.size(); i++) {
            auto &planet = planets[i];

            double curOrbit = glm::mix((double)planet.orbitRadius, (double)planet.realOrbit, (double)easeScale);

            double a = curOrbit; double e = planet.eccentricity; double b = a * sqrt(1.0 - e * e); double c = a * e;

            double angle = glm::radians((double)planet.initialAngle) + (double)slowTime * glm::radians((double)planet.orbitSpeed);
            glm::dvec3 localPos = glm::dvec3(a * cos(angle) - c, 0.0, b * sin(angle));

            glm::dmat4 orbitTilt = glm::rotate(glm::dmat4(1.0), glm::radians((double)planet.ascendingNode), glm::dvec3(0.0, 1.0, 0.0))
                                 * glm::rotate(glm::dmat4(1.0), glm::radians((double)planet.orbitalInclination), glm::dvec3(0.0, 0.0, 1.0))
                                 * glm::rotate(glm::dmat4(1.0), glm::radians((double)planet.periapsisAngle), glm::dvec3(0.0, 1.0, 0.0));

            glm::dvec3 parentPos = glm::dvec3(0.0);
            if (planet.parentIndex != -1) {
                parentPos = planets[planet.parentIndex].currentPosition;
            }

            planet.currentPosition = parentPos + glm::dvec3(orbitTilt * glm::dvec4(localPos, 1.0));

            // 달은 지구(index 2)를 부모로 삼아 위치를 잡는다.
            if (i == 2) {
                double curMoonOrbit = glm::mix((double)moon.orbitRadius, (double)moon.realOrbit, (double)easeScale);

                double ma = curMoonOrbit, me = moon.eccentricity, mb = ma * sqrt(1.0 - me * me), mc = ma * me;
                double mAngle = glm::radians((double)moon.initialAngle) + (double)slowTime * glm::radians((double)moon.orbitSpeed);
                glm::dvec3 mLocal = glm::dvec3(ma * cos(mAngle) - mc, 0.0, mb * sin(mAngle));

                glm::dmat4 mOrbitTilt = glm::rotate(glm::dmat4(1.0), glm::radians((double)moon.ascendingNode), glm::dvec3(0.0, 1.0, 0.0))
                                      * glm::rotate(glm::dmat4(1.0), glm::radians((double)moon.orbitalInclination), glm::dvec3(0.0, 0.0, 1.0))
                                      * glm::rotate(glm::dmat4(1.0), glm::radians((double)moon.periapsisAngle), glm::dvec3(0.0, 1.0, 0.0));

                moon.currentPosition = planet.currentPosition + glm::dvec3(mOrbitTilt * glm::dvec4(mLocal, 1.0));
            }
        }
```

- [ ] **Step 3: Add the model-matrix pass**

Immediately above `void updateUniformBuffer() override {` in `src/main.cpp`, add:

```cpp
    // 천체들의 위치가 확정되고 카메라까지 갱신된 뒤에 호출된다.
    // 모델 행렬은 카메라 상대 좌표를 쓰므로 반드시 이번 프레임의 camera.target 확정 후여야 한다.
    void buildBodyModelMatrices(float easeScale, float slowTime, float time) {
        float curSunRadius = glm::mix(sun.radius, sun.realRadius, easeScale);
        sun.currentModelMat = glm::translate(glm::mat4(1.0f), glm::vec3(sun.currentPosition))
                            * glm::rotate(glm::mat4(1.0f), glm::radians(sun.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                            * glm::rotate(glm::mat4(1.0f), glm::radians(sun.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                            * glm::rotate(glm::mat4(1.0f), time * glm::radians(sun.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f))
                            * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius));

        for (int i = 0; i < planets.size(); i++) {
            auto &planet = planets[i];
            float curRadius = glm::mix(planet.radius, planet.realRadius, easeScale);

            glm::mat4 trajectory = glm::translate(glm::mat4(1.0f), glm::vec3(planet.currentPosition));

            glm::mat4 selfRotationMat = glm::rotate(glm::mat4(1.0f), glm::radians(planet.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                                      * glm::rotate(glm::mat4(1.0f), glm::radians(planet.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                                      * glm::rotate(glm::mat4(1.0f), slowTime * glm::radians(planet.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f));

            planet.currentModelMat = trajectory * selfRotationMat * glm::scale(glm::mat4(1.0f), glm::vec3(curRadius));

            if (planet.hasClouds) {
                glm::mat4 cloudSelfRot = glm::rotate(glm::mat4(1.0f), glm::radians(planet.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                                       * glm::rotate(glm::mat4(1.0f), glm::radians(planet.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                                       * glm::rotate(glm::mat4(1.0f), slowTime * glm::radians(planet.rotationSpeed + 4.0f), glm::vec3(0.0f, 1.0f, 0.0f));
                planet.cloudModelMat = trajectory * cloudSelfRot * glm::scale(glm::mat4(1.0f), glm::vec3(curRadius * 1.005f));
            }
        }

        float curMoonRadius = glm::mix(moon.radius, moon.realRadius, easeScale);
        glm::mat4 moonBase = glm::translate(glm::mat4(1.0f), glm::vec3(moon.currentPosition));
        glm::mat4 moonRot = glm::rotate(glm::mat4(1.0f), glm::radians(moon.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f))
                          * glm::rotate(glm::mat4(1.0f), glm::radians(moon.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f))
                          * glm::rotate(glm::mat4(1.0f), slowTime * glm::radians(moon.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f));
        moon.currentModelMat = moonBase * moonRot * glm::scale(glm::mat4(1.0f), glm::vec3(curMoonRadius));
    }

```

- [ ] **Step 4: Delete the now-duplicated sun model-matrix lines from the position pass**

In `updateUniformBuffer`, find:

```cpp
        // 🚀 1. 태양(Sun) 크기 믹스 적용
        float curSunRadius = glm::mix(sun.radius, sun.realRadius, easeScale);
        sun.currentModelMat = glm::rotate(glm::mat4(1.0f), glm::radians(sun.axisDirection), glm::vec3(0.0f, 1.0f, 0.0f)) 
                            * glm::rotate(glm::mat4(1.0f), glm::radians(sun.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f)) 
                            * glm::rotate(glm::mat4(1.0f), time * glm::radians(sun.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f)) 
                            * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius));
        sun.currentPosition = glm::vec3(0.0f);
```

Replace with:

```cpp
        // 🚀 1. 태양(Sun)은 월드 원점에 고정. 모델 행렬은 buildBodyModelMatrices()에서 만든다.
        sun.currentPosition = glm::dvec3(0.0);
```

Also find this earlier pair of lines near the top of `updateUniformBuffer` and delete them (they are dead — both values are overwritten below):

```cpp
        sun.currentModelMat = glm::rotate(glm::mat4(1.0f), glm::radians(sun.axialTilt), glm::vec3(0.0f, 0.0f, 1.0f)) * glm::rotate(glm::mat4(1.0f), time * glm::radians(sun.rotationSpeed), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(sun.radius));
        sun.currentPosition = glm::vec3(0.0f);
```

- [ ] **Step 5: Call the new pass after the camera update**

Find:

```cpp
        camera.smoothFollow(glm::dvec3(nextTarget), deltaTime, targetChanged);
        camera.update(deltaTime);
```

Replace with:

```cpp
        camera.smoothFollow(glm::dvec3(nextTarget), deltaTime, targetChanged);
        camera.update(deltaTime);
```

Then find the end of the cinematic-eclipse block:

```cpp
            camera.currentDistance = glm::mix(camera.currentDistance, targetZoom, ease);
        }

        UniformBufferObject ubo{};
```

Replace with:

```cpp
            camera.currentDistance = glm::mix(camera.currentDistance, targetZoom, ease);
        }

        // 반드시 일식 블록 '뒤'여야 한다. 저 블록이 camera.target을 한 번 더 덮어쓰기 때문에,
        // 여기보다 위에서 모델 행렬을 만들면 모델은 일식 전 타겟을, 뷰 행렬은 일식 후 타겟을
        // 보게 되어 카메라 상대 좌표계에서 천체가 통째로 어긋난다.
        buildBodyModelMatrices(easeScale, slowTime, time);

        UniformBufferObject ubo{};
```

**Why not right after `camera.update()`:** the cinematic-eclipse block below it assigns
`camera.target` again (`camera.target = glm::mix(camera.target, glm::dvec3(moonPos), ease)`), and
`ubo.view` is built from that final value. `camera.target` is only truly final after that block.
The eclipse block does not touch `easeScale` / `slowTime` / `time`, so this placement is a no-op
today and correct once Task 5 makes model matrices camera-relative.

- [ ] **Step 6: Fix the remaining `currentPosition` consumers**

`Planet::currentPosition` is now `dvec3`. Every read that feeds a `glm::vec3` needs an explicit conversion. Apply each:

Find:

```cpp
        if (check2DBoxHit(sun.currentPosition, "Sun", 0, -1, 35.0f)) tagHit = true;
        if (!tagHit) {
            for (int i = 0; i < planets.size(); i++) {
                if (check2DBoxHit(planets[i].currentPosition, planets[i].name, 1, i, 35.0f)) { tagHit = true; break; }
            }
        }
        if (!tagHit) {
            if (check2DBoxHit(moon.currentPosition, "Moon", 2, -1, 25.0f)) tagHit = true;
        }
```

Replace with:

```cpp
        if (check2DBoxHit(glm::vec3(sun.currentPosition), "Sun", 0, -1, 35.0f)) tagHit = true;
        if (!tagHit) {
            for (int i = 0; i < planets.size(); i++) {
                if (check2DBoxHit(glm::vec3(planets[i].currentPosition), planets[i].name, 1, i, 35.0f)) { tagHit = true; break; }
            }
        }
        if (!tagHit) {
            if (check2DBoxHit(glm::vec3(moon.currentPosition), "Moon", 2, -1, 25.0f)) tagHit = true;
        }
```

Find:

```cpp
            checkIntersect(sun.currentPosition, curSunRad, 0, -1);
            for (int i = 0; i < planets.size(); i++) {
                float curPlanetRad = glm::mix(planets[i].radius, planets[i].realRadius, easeScale);
                checkIntersect(planets[i].currentPosition, curPlanetRad, 1, i);
            }
            checkIntersect(moon.currentPosition, curMoonRad, 2, -1);
```

Replace with:

```cpp
            checkIntersect(glm::vec3(sun.currentPosition), curSunRad, 0, -1);
            for (int i = 0; i < planets.size(); i++) {
                float curPlanetRad = glm::mix(planets[i].radius, planets[i].realRadius, easeScale);
                checkIntersect(glm::vec3(planets[i].currentPosition), curPlanetRad, 1, i);
            }
            checkIntersect(glm::vec3(moon.currentPosition), curMoonRad, 2, -1);
```

Find:

```cpp
        if (lockedTargetType == 0) { 
            nextTarget = sun.currentPosition; 
```

Replace with:

```cpp
        if (lockedTargetType == 0) { 
            nextTarget = glm::vec3(sun.currentPosition); 
```

Find:

```cpp
            nextTarget = planets[lockedPlanetIndex].currentPosition; 
```

Replace with:

```cpp
            nextTarget = glm::vec3(planets[lockedPlanetIndex].currentPosition); 
```

Find:

```cpp
            nextTarget = moon.currentPosition; 
```

Replace with:

```cpp
            nextTarget = glm::vec3(moon.currentPosition); 
```

Find:

```cpp
            glm::vec3 sunPos = sun.currentPosition;
            glm::vec3 moonPos = moon.currentPosition;
```

Replace with:

```cpp
            glm::vec3 sunPos = glm::vec3(sun.currentPosition);
            glm::vec3 moonPos = glm::vec3(moon.currentPosition);
```

Find:

```cpp
        ubo.sunPos = sun.currentPosition; ubo.sunRadius = sun.radius;
        ubo.earthPos = planets[2].currentPosition; ubo.earthRadius = planets[2].radius;
        ubo.moonPos = moon.currentPosition; ubo.moonRadius = moon.radius;
```

Replace with:

```cpp
        ubo.sunPos = glm::vec3(sun.currentPosition); ubo.sunRadius = sun.radius;
        ubo.earthPos = glm::vec3(planets[2].currentPosition); ubo.earthRadius = planets[2].radius;
        ubo.moonPos = glm::vec3(moon.currentPosition); ubo.moonRadius = moon.radius;
```

Find:

```cpp
        glm::vec3 shadowFocusPos = nextTarget;
        if (lockedTargetType == 0) shadowFocusPos = planets[2].currentPosition;
```

Replace with:

```cpp
        glm::vec3 shadowFocusPos = nextTarget;
        if (lockedTargetType == 0) shadowFocusPos = glm::vec3(planets[2].currentPosition);
```

Find:

```cpp
        glm::vec3 lightDir = glm::normalize(shadowFocusPos - sun.currentPosition);
```

Replace with:

```cpp
        glm::vec3 lightDir = glm::normalize(shadowFocusPos - glm::vec3(sun.currentPosition));
```

Find:

```cpp
        glm::mat4 lightView = glm::lookAt(sun.currentPosition, shadowFocusPos, up);
```

Replace with:

```cpp
        glm::mat4 lightView = glm::lookAt(glm::vec3(sun.currentPosition), shadowFocusPos, up);
```

Find:

```cpp
        glm::vec3 currentCameraPos = camera.target + camera.pos;
        float distToSun = glm::length(currentCameraPos - sun.currentPosition);
```

Replace with:

```cpp
        glm::vec3 currentCameraPos = glm::vec3(camera.getEyeWorld());
        float distToSun = glm::length(currentCameraPos - glm::vec3(sun.currentPosition));
```

Find (in the orbit-line block of `recordCommandBuffer`):

```cpp
                if (planet.parentIndex != -1) orbitCenter = glm::translate(glm::mat4(1.0f), planets[planet.parentIndex].currentPosition);
```

Replace with:

```cpp
                if (planet.parentIndex != -1) orbitCenter = glm::translate(glm::mat4(1.0f), glm::vec3(planets[planet.parentIndex].currentPosition));
```

Find:

```cpp
            glm::mat4 moonOrbitModel = glm::translate(glm::mat4(1.0f), planets[2].currentPosition) * mOrbitTilt
```

Replace with:

```cpp
            glm::mat4 moonOrbitModel = glm::translate(glm::mat4(1.0f), glm::vec3(planets[2].currentPosition)) * mOrbitTilt
```

Find:

```cpp
            glm::mat4 haloModel = glm::translate(glm::mat4(1.0f), sun.currentPosition) * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius * 2.5f));
```

Replace with:

```cpp
            glm::mat4 haloModel = glm::translate(glm::mat4(1.0f), glm::vec3(sun.currentPosition)) * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius * 2.5f));
```

Find (in `recordCommandBuffer`, the nametag/drawTagBox area of `updateUniformBuffer`):

```cpp
            drawTagBox(sun.currentPosition, "Sun", IM_COL32(255, 200, 0, 255), 35.0f, (selectedTargetType == 0));
            for (int i = 0; i < planets.size(); i++) {
                drawTagBox(planets[i].currentPosition, planets[i].name, IM_COL32(255, 255, 255, 200), 35.0f, (selectedTargetType == 1 && selectedPlanetIndex == i));
            }
            drawTagBox(moon.currentPosition, "Moon", IM_COL32(200, 200, 200, 200), 25.0f, (selectedTargetType == 2));
```

Replace with:

```cpp
            drawTagBox(glm::vec3(sun.currentPosition), "Sun", IM_COL32(255, 200, 0, 255), 35.0f, (selectedTargetType == 0));
            for (int i = 0; i < planets.size(); i++) {
                drawTagBox(glm::vec3(planets[i].currentPosition), planets[i].name, IM_COL32(255, 255, 255, 200), 35.0f, (selectedTargetType == 1 && selectedPlanetIndex == i));
            }
            drawTagBox(glm::vec3(moon.currentPosition), "Moon", IM_COL32(200, 200, 200, 200), 25.0f, (selectedTargetType == 2));
```

- [ ] **Step 7: Build**

Run: `"$CMAKE" --build build --config Debug`
Expected: succeeds. If the compiler flags any other `dvec3`→`vec3` mismatch, wrap that read in `glm::vec3(...)` the same way and note it in your report.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "Compute body positions in double; build model matrices in a second pass

Positions go to double so worldPos - cameraTarget can be exact. Model
matrices move to a pass that runs after the camera update, because once they
are camera-relative they must see this frame's camera.target -- otherwise the
model and view matrices disagree by a frame of camera motion (5.9 units for
Eris). Rendering still uses the absolute frame."
```

---

## Task 4: Route every position through a `relativeToCamera()` seam

Introduce the seam that Task 5 will flip. The helper returns the absolute position for now, so this task is a **no-op** — but it moves every world position in the renderer onto a single line of code.

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: `glm::vec3 SolarSystemApp::relativeToCamera(const glm::dvec3& worldPos) const` — converts a world position into the frame the renderer draws in. Returns absolute coordinates until Task 5.

- [ ] **Step 1: Add the helper**

Immediately above `void buildBodyModelMatrices(float easeScale, float slowTime, float time) {` in `src/main.cpp`, add:

```cpp
    // 렌더러가 그리는 좌표계로 월드 좌표를 옮긴다. 모든 모델 행렬/UBO 좌표/피킹이 이 함수를 거친다.
    // 지금은 절대 좌표를 그대로 돌려주지만, 카메라 상대 좌표로 전환할 때 여기 한 곳만 바꾸면 된다.
    glm::vec3 relativeToCamera(const glm::dvec3& worldPos) const {
        return glm::vec3(worldPos);
    }

```

- [ ] **Step 2: Route the body model matrices through it**

In `buildBodyModelMatrices`, find:

```cpp
        sun.currentModelMat = glm::translate(glm::mat4(1.0f), glm::vec3(sun.currentPosition))
```

Replace with:

```cpp
        sun.currentModelMat = glm::translate(glm::mat4(1.0f), relativeToCamera(sun.currentPosition))
```

Find:

```cpp
            glm::mat4 trajectory = glm::translate(glm::mat4(1.0f), glm::vec3(planet.currentPosition));
```

Replace with:

```cpp
            glm::mat4 trajectory = glm::translate(glm::mat4(1.0f), relativeToCamera(planet.currentPosition));
```

Find:

```cpp
        glm::mat4 moonBase = glm::translate(glm::mat4(1.0f), glm::vec3(moon.currentPosition));
```

Replace with:

```cpp
        glm::mat4 moonBase = glm::translate(glm::mat4(1.0f), relativeToCamera(moon.currentPosition));
```

- [ ] **Step 3: Route the UBO positions through it**

Find:

```cpp
        ubo.cameraPos = glm::vec3(camera.getEyeWorld());
        ubo.time = time;
        ubo.sunPos = glm::vec3(sun.currentPosition); ubo.sunRadius = sun.radius;
        ubo.earthPos = glm::vec3(planets[2].currentPosition); ubo.earthRadius = planets[2].radius;
        ubo.moonPos = glm::vec3(moon.currentPosition); ubo.moonRadius = moon.radius;
```

Replace with:

```cpp
        ubo.cameraPos = relativeToCamera(camera.getEyeWorld());
        ubo.time = time;
        ubo.sunPos = relativeToCamera(sun.currentPosition); ubo.sunRadius = sun.radius;
        ubo.earthPos = relativeToCamera(planets[2].currentPosition); ubo.earthRadius = planets[2].radius;
        ubo.moonPos = relativeToCamera(moon.currentPosition); ubo.moonRadius = moon.radius;
```

- [ ] **Step 4: Route the shadow matrices through it**

Note: the two lines below are separated in the file by a whitespace-only line. Do these as two separate edits rather than one block match.

First, find:

```cpp
        glm::vec3 shadowFocusPos = nextTarget;
        if (lockedTargetType == 0) shadowFocusPos = glm::vec3(planets[2].currentPosition);
```

Replace with:

```cpp
        glm::dvec3 shadowFocusWorld = glm::dvec3(nextTarget);
        if (lockedTargetType == 0) shadowFocusWorld = planets[2].currentPosition;
        glm::vec3 shadowFocusPos = relativeToCamera(shadowFocusWorld);
        glm::vec3 sunPosRel = relativeToCamera(sun.currentPosition);
```

Then, find:

```cpp
        glm::vec3 lightDir = glm::normalize(shadowFocusPos - glm::vec3(sun.currentPosition));
```

Replace with:

```cpp
        glm::vec3 lightDir = glm::normalize(shadowFocusPos - sunPosRel);
```

Then, find:

```cpp
        glm::mat4 lightView = glm::lookAt(glm::vec3(sun.currentPosition), shadowFocusPos, up);
```

Replace with:

```cpp
        glm::mat4 lightView = glm::lookAt(sunPosRel, shadowFocusPos, up);
```

- [ ] **Step 5: Route picking and nametags through it**

Find:

```cpp
            glm::vec3 rayOrigin = glm::vec3(camera.getEyeWorld());
```

Replace with:

```cpp
            glm::vec3 rayOrigin = relativeToCamera(camera.getEyeWorld());
```

Find:

```cpp
            checkIntersect(glm::vec3(sun.currentPosition), curSunRad, 0, -1);
            for (int i = 0; i < planets.size(); i++) {
                float curPlanetRad = glm::mix(planets[i].radius, planets[i].realRadius, easeScale);
                checkIntersect(glm::vec3(planets[i].currentPosition), curPlanetRad, 1, i);
            }
            checkIntersect(glm::vec3(moon.currentPosition), curMoonRad, 2, -1);
```

Replace with:

```cpp
            checkIntersect(relativeToCamera(sun.currentPosition), curSunRad, 0, -1);
            for (int i = 0; i < planets.size(); i++) {
                float curPlanetRad = glm::mix(planets[i].radius, planets[i].realRadius, easeScale);
                checkIntersect(relativeToCamera(planets[i].currentPosition), curPlanetRad, 1, i);
            }
            checkIntersect(relativeToCamera(moon.currentPosition), curMoonRad, 2, -1);
```

Find:

```cpp
        if (check2DBoxHit(glm::vec3(sun.currentPosition), "Sun", 0, -1, 35.0f)) tagHit = true;
        if (!tagHit) {
            for (int i = 0; i < planets.size(); i++) {
                if (check2DBoxHit(glm::vec3(planets[i].currentPosition), planets[i].name, 1, i, 35.0f)) { tagHit = true; break; }
            }
        }
        if (!tagHit) {
            if (check2DBoxHit(glm::vec3(moon.currentPosition), "Moon", 2, -1, 25.0f)) tagHit = true;
        }
```

Replace with:

```cpp
        if (check2DBoxHit(relativeToCamera(sun.currentPosition), "Sun", 0, -1, 35.0f)) tagHit = true;
        if (!tagHit) {
            for (int i = 0; i < planets.size(); i++) {
                if (check2DBoxHit(relativeToCamera(planets[i].currentPosition), planets[i].name, 1, i, 35.0f)) { tagHit = true; break; }
            }
        }
        if (!tagHit) {
            if (check2DBoxHit(relativeToCamera(moon.currentPosition), "Moon", 2, -1, 25.0f)) tagHit = true;
        }
```

Find:

```cpp
            drawTagBox(glm::vec3(sun.currentPosition), "Sun", IM_COL32(255, 200, 0, 255), 35.0f, (selectedTargetType == 0));
            for (int i = 0; i < planets.size(); i++) {
                drawTagBox(glm::vec3(planets[i].currentPosition), planets[i].name, IM_COL32(255, 255, 255, 200), 35.0f, (selectedTargetType == 1 && selectedPlanetIndex == i));
            }
            drawTagBox(glm::vec3(moon.currentPosition), "Moon", IM_COL32(200, 200, 200, 200), 25.0f, (selectedTargetType == 2));
```

Replace with:

```cpp
            drawTagBox(relativeToCamera(sun.currentPosition), "Sun", IM_COL32(255, 200, 0, 255), 35.0f, (selectedTargetType == 0));
            for (int i = 0; i < planets.size(); i++) {
                drawTagBox(relativeToCamera(planets[i].currentPosition), planets[i].name, IM_COL32(255, 255, 255, 200), 35.0f, (selectedTargetType == 1 && selectedPlanetIndex == i));
            }
            drawTagBox(relativeToCamera(moon.currentPosition), "Moon", IM_COL32(200, 200, 200, 200), 25.0f, (selectedTargetType == 2));
```

- [ ] **Step 6: Route the sun halo, asteroid belt, milky way, and orbit lines through it**

Find:

```cpp
            glm::mat4 haloModel = glm::translate(glm::mat4(1.0f), glm::vec3(sun.currentPosition)) * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius * 2.5f));
```

Replace with:

```cpp
            glm::mat4 haloModel = glm::translate(glm::mat4(1.0f), relativeToCamera(sun.currentPosition)) * glm::scale(glm::mat4(1.0f), glm::vec3(curSunRadius * 2.5f));
```

Find:

```cpp
        glm::mat4 astRot = glm::rotate(glm::mat4(1.0f), currentAppTime * 0.05f, glm::vec3(0.0f, 1.0f, 0.0f)); 
        astRot = glm::scale(astRot, glm::vec3(beltScale)); // 전체 입자에 스케일 적용!
```

Replace with:

```cpp
        // 소행성 행렬(SSBO)은 월드 원점 기준이므로, 원점을 렌더 좌표계로 옮겨 앞에 붙인다.
        glm::mat4 astRot = glm::translate(glm::mat4(1.0f), relativeToCamera(glm::dvec3(0.0)))
                         * glm::rotate(glm::mat4(1.0f), currentAppTime * 0.05f, glm::vec3(0.0f, 1.0f, 0.0f));
        astRot = glm::scale(astRot, glm::vec3(beltScale)); // 전체 입자에 스케일 적용!
```

Find:

```cpp
        glm::mat4 mwModel = glm::translate(glm::mat4(1.0f), glm::vec3(galaxyOffsetX, galaxyDrop, 0.0f)) 
```

Replace with:

```cpp
        glm::mat4 mwModel = glm::translate(glm::mat4(1.0f), relativeToCamera(glm::dvec3(galaxyOffsetX, galaxyDrop, 0.0)))
```

Find:

```cpp
                glm::mat4 orbitCenter = glm::mat4(1.0f);
                if (planet.parentIndex != -1) orbitCenter = glm::translate(glm::mat4(1.0f), glm::vec3(planets[planet.parentIndex].currentPosition));
```

Replace with:

```cpp
                glm::dvec3 orbitCenterWorld = glm::dvec3(0.0);
                if (planet.parentIndex != -1) orbitCenterWorld = planets[planet.parentIndex].currentPosition;
                glm::mat4 orbitCenter = glm::translate(glm::mat4(1.0f), relativeToCamera(orbitCenterWorld));
```

Find:

```cpp
            glm::mat4 moonOrbitModel = glm::translate(glm::mat4(1.0f), glm::vec3(planets[2].currentPosition)) * mOrbitTilt
```

Replace with:

```cpp
            glm::mat4 moonOrbitModel = glm::translate(glm::mat4(1.0f), relativeToCamera(planets[2].currentPosition)) * mOrbitTilt
```

- [ ] **Step 7: Build**

Run: `"$CMAKE" --build build --config Debug`
Expected: succeeds, no errors.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "Route every renderer position through relativeToCamera()

The helper still returns absolute coordinates, so this is a no-op. It puts
the whole renderer's coordinate frame behind one function."
```

---

## Task 5: Flip to the camera-relative frame

The payload. Two edits, and the precision problem is gone.

**Files:**
- Modify: `src/main.cpp` (`relativeToCamera` body)
- Modify: `src/Camera.hpp` (`getViewMatrix`)

**Interfaces:**
- Consumes: `relativeToCamera()` (Task 4), `Camera::target` as `dvec3` (Task 2)

- [ ] **Step 1: Make `relativeToCamera` actually subtract the camera target**

In `src/main.cpp`, find:

```cpp
    // 렌더러가 그리는 좌표계로 월드 좌표를 옮긴다. 모든 모델 행렬/UBO 좌표/피킹이 이 함수를 거친다.
    // 지금은 절대 좌표를 그대로 돌려주지만, 카메라 상대 좌표로 전환할 때 여기 한 곳만 바꾸면 된다.
    glm::vec3 relativeToCamera(const glm::dvec3& worldPos) const {
        return glm::vec3(worldPos);
    }
```

Replace with:

```cpp
    // 렌더러가 그리는 좌표계로 월드 좌표를 옮긴다. 모든 모델 행렬/UBO 좌표/피킹이 이 함수를 거친다.
    // 뺄셈을 double로 해야 하는 이유: 리얼 스케일에서 두 항이 모두 46000 수준이라 float32로 빼면
    // 결과(수십분의 1 유닛)에 0.0039짜리 오차가 남아 왜소행성(반지름 0.09)이 떨리고 각져 보인다.
    glm::vec3 relativeToCamera(const glm::dvec3& worldPos) const {
        return glm::vec3(worldPos - camera.target);
    }
```

- [ ] **Step 2: Put the camera at the origin of the view**

In `src/Camera.hpp`, find:

```cpp
    glm::mat4 getViewMatrix() const
    {
        // =========================================================
        // 🚀 [NEW] 기존의 가변 up 벡터를 지우고, 절대 고정된 WORLD_UP을 사용합니다!
        // =========================================================
        return glm::lookAt(glm::vec3(target) + pos, glm::vec3(target), WORLD_UP);
    }
```

Replace with:

```cpp
    // 렌더링은 카메라 상대 좌표계에서 이뤄진다. 이 좌표계에서 타겟은 정의상 원점이고
    // 눈은 그로부터 pos만큼 떨어져 있다. (relativeToCamera()가 같은 규약을 쓴다)
    glm::mat4 getViewMatrix() const
    {
        // =========================================================
        // 🚀 [NEW] 기존의 가변 up 벡터를 지우고, 절대 고정된 WORLD_UP을 사용합니다!
        // =========================================================
        return glm::lookAt(pos, glm::vec3(0.0f), WORLD_UP);
    }
```

- [ ] **Step 3: Build**

Run: `"$CMAKE" --build build --config Debug`
Expected: succeeds, no errors.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp src/Camera.hpp
git commit -m "Render in a camera-relative frame

relativeToCamera() now subtracts camera.target in double, and the view puts
the camera at the origin. GPU vertex math happens near zero instead of near
46000, where float32's 0.0039 ULP was quantising a 0.09-radius dwarf planet
into a faceted, jittering blob."
```

---

## Task 6: Draw the locked body's orbit line from a per-frame, double-precision buffer

The orbit line is still a unit circle scaled to ~34000 by its model matrix, so the GPU evaluates `34000*cos(θ)` and subtracts a ~46000 translation in float32 — cancellation that leaves ~14 px of shimmer on the one orbit the user is actually looking at. Generate that single orbit on the CPU in double instead.

Only the locked body's orbit needs this: orbit radius is fixed per body, so the orbit nearest the camera is by definition the locked body's. Every other orbit is tens of thousands of units away, where the same absolute error is angularly invisible.

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `relativeToCamera()` (Task 4), `Planet::currentPosition` as `dvec3` (Task 3)
- Produces: `VkBuffer lockedOrbitBuffer`, `VkDeviceMemory lockedOrbitMemory`, `void* lockedOrbitMapped`, `bool lockedOrbitValid`
- Produces: `void SolarSystemApp::updateLockedOrbitLine(float easeScale)` — rebuilds the vertex data each frame; sets `lockedOrbitValid`
- Produces: `static constexpr int LOCKED_ORBIT_SEGMENTS = 4096`

- [ ] **Step 1: Declare the buffer members**

In `src/main.cpp`, find:

```cpp
    uint32_t sphereIndexCount = 0, ringIndexCount = 0, orbitIndexCount = 0, orbitFirstIndex = 0;
```

Add immediately above it:

```cpp
    // 고정된 천체의 궤도선은 매 프레임 CPU에서 double로, 카메라 상대 좌표로 다시 만든다.
    // 공유 단위원 + 모델 행렬 방식으로는 GPU가 34000짜리 항끼리 빼면서 정밀도를 잃기 때문.
    static constexpr int LOCKED_ORBIT_SEGMENTS = 4096;
    VkBuffer lockedOrbitBuffer = VK_NULL_HANDLE;
    VkDeviceMemory lockedOrbitMemory = VK_NULL_HANDLE;
    void* lockedOrbitMapped = nullptr;
    bool lockedOrbitValid = false;
    std::vector<Vertex> lockedOrbitScratch; // 매 프레임 재사용해 힙 할당을 피한다

```

- [ ] **Step 2: Create the buffer**

In `initApp()`, find:

```cpp
        createVertexBuffer(); createIndexBuffer(); createUniformBuffer();
```

Replace with:

```cpp
        createVertexBuffer(); createIndexBuffer(); createUniformBuffer();
        createLockedOrbitBuffer();
```

Immediately above `void createVertexBuffer() {` in `src/main.cpp`, add:

```cpp
    void createLockedOrbitBuffer() {
        VkDeviceSize bufferSize = sizeof(Vertex) * (LOCKED_ORBIT_SEGMENTS + 1);
        createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     lockedOrbitBuffer, lockedOrbitMemory);
        vkMapMemory(device, lockedOrbitMemory, 0, bufferSize, 0, &lockedOrbitMapped);
    }

```

- [ ] **Step 3: Generate the vertices each frame**

Immediately above `void createLockedOrbitBuffer() {`, add:

```cpp
    // 고정된 천체의 궤도를 double로 계산해 카메라 상대 좌표 정점으로 굽는다.
    // 궤도 자체는 부모(태양 또는 모행성) 기준이므로 부모 위치를 더해 월드 좌표를 만든 뒤 상대화한다.
    void updateLockedOrbitLine(float easeScale) {
        lockedOrbitValid = false;
        if (lockedTargetType != 1 || lockedPlanetIndex < 0 || lockedPlanetIndex >= (int)planets.size()) return;

        const Planet& p = planets[lockedPlanetIndex];

        double a = glm::mix((double)p.orbitRadius, (double)p.realOrbit, (double)easeScale);
        double e = p.eccentricity;
        double b = a * sqrt(1.0 - e * e);
        double c = a * e;

        glm::dmat4 tilt = glm::rotate(glm::dmat4(1.0), glm::radians((double)p.ascendingNode), glm::dvec3(0.0, 1.0, 0.0))
                        * glm::rotate(glm::dmat4(1.0), glm::radians((double)p.orbitalInclination), glm::dvec3(0.0, 0.0, 1.0))
                        * glm::rotate(glm::dmat4(1.0), glm::radians((double)p.periapsisAngle), glm::dvec3(0.0, 1.0, 0.0));

        glm::dvec3 parentPos = glm::dvec3(0.0);
        if (p.parentIndex != -1) parentPos = planets[p.parentIndex].currentPosition;

        lockedOrbitScratch.resize(LOCKED_ORBIT_SEGMENTS + 1);
        for (int i = 0; i <= LOCKED_ORBIT_SEGMENTS; ++i) {
            double ang = (double)i / (double)LOCKED_ORBIT_SEGMENTS * 2.0 * M_PI;
            glm::dvec3 local = glm::dvec3(a * cos(ang) - c, 0.0, b * sin(ang));
            glm::dvec3 world = parentPos + glm::dvec3(tilt * glm::dvec4(local, 1.0));
            lockedOrbitScratch[i] = {relativeToCamera(world), glm::vec3(0.2f, 0.4f, 0.0f), glm::vec2(0.0f), glm::vec3(0, 1, 0)};
        }

        memcpy(lockedOrbitMapped, lockedOrbitScratch.data(), sizeof(Vertex) * lockedOrbitScratch.size());
        lockedOrbitValid = true;
    }

```

- [ ] **Step 4: Call it once the camera target is final**

Find:

```cpp
        buildBodyModelMatrices(easeScale, slowTime, time);
```

Replace with:

```cpp
        buildBodyModelMatrices(easeScale, slowTime, time);
        updateLockedOrbitLine(easeScale);
```

- [ ] **Step 5: Draw the locked orbit from the dedicated buffer and skip its shared-geometry copy**

In `recordCommandBuffer`, find:

```cpp
            for (const auto &planet : planets) {
                float curOrbit = glm::mix(planet.orbitRadius, planet.realOrbit, scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp));
```

Replace with:

```cpp
            for (int pi = 0; pi < (int)planets.size(); ++pi) {
                const auto &planet = planets[pi];
                // 고정된 천체의 궤도는 아래에서 고정밀 버퍼로 따로 그린다.
                if (lockedOrbitValid && lockedTargetType == 1 && pi == lockedPlanetIndex) continue;
                float curOrbit = glm::mix(planet.orbitRadius, planet.realOrbit, scaleLerp * scaleLerp * (3.0f - 2.0f * scaleLerp));
```

Then find the end of that loop:

```cpp
                PushConstants orbitPush{orbitModel, 6}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &orbitPush);
                vkCmdDrawIndexed(cb, orbitIndexCount, 1, orbitFirstIndex, orbitVertexOffset, 0);
            }
```

Replace with:

```cpp
                PushConstants orbitPush{orbitModel, 6}; vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &orbitPush);
                vkCmdDrawIndexed(cb, orbitIndexCount, 1, orbitFirstIndex, orbitVertexOffset, 0);
            }

            // 고정된 천체의 궤도선: 이미 카메라 상대 좌표로 구워져 있으므로 모델 행렬은 단위 행렬.
            if (lockedOrbitValid) {
                VkDeviceSize lockedOffset = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &lockedOrbitBuffer, &lockedOffset);
                PushConstants lockedPush{glm::mat4(1.0f), 6};
                vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &lockedPush);
                vkCmdDraw(cb, LOCKED_ORBIT_SEGMENTS + 1, 1, 0, 0);
                vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets);
            }
```

- [ ] **Step 6: Free the buffer on shutdown**

In `cleanupApp()`, find:

```cpp
        vkDestroyBuffer(device, uniformBuffer, nullptr); vkFreeMemory(device, uniformBufferMemory, nullptr);
```

Replace with:

```cpp
        vkDestroyBuffer(device, uniformBuffer, nullptr); vkFreeMemory(device, uniformBufferMemory, nullptr);
        vkDestroyBuffer(device, lockedOrbitBuffer, nullptr); vkFreeMemory(device, lockedOrbitMemory, nullptr);
```

- [ ] **Step 7: Build**

Run: `"$CMAKE" --build build --config Debug`
Expected: succeeds, no errors.

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "Bake the locked body's orbit line in double, camera-relative

The shared unit circle is scaled to ~34000 by its model matrix, so the GPU
cancels 34000-magnitude terms against a 46000 translation in float32 and the
line shimmers ~14px next to a now-rock-steady planet. Only the locked body's
orbit can be near the camera, so only it needs baking: 4096 verts, 180KB/frame."
```

---

## Task 7: Verify precision end-to-end

**Files:** None (verification only)

- [ ] **Step 1: Clean rebuild**

Run: `"$CMAKE" --build build --config Debug --clean-first`
Expected: succeeds with no errors or new warnings.

- [ ] **Step 2: Re-run the precision harness against the patched `Camera.hpp`**

The harness at `jitter_test.cpp` (in the session scratchpad) drives the real `Camera::smoothFollow` and reports faceting and jitter for Eris at real scale. Its `RELATIVE_F64` mode models exactly what Tasks 2-5 implement.

Expected, matching the design spec's targets:
```
camera-relative, double            0.000 %        0.00 px <-- clean
```

Report the actual numbers. If `RELATIVE_F64` no longer reports 0.000% / 0.00 px, the implementation diverged from the design — stop and report rather than patching.

- [ ] **Step 3: Report what still needs a human**

The following cannot be verified from this environment (synthetic mouse input does not reach the app) and must be confirmed by the user:

1. Real scale + double-click Makemake/Eris + zoom in → body is a smooth sphere, not faceted; body does not shake.
2. Orbit lines ON + locked on a dwarf planet → the body sits on its orbit line, and the line does not shimmer.
3. Saturn's ring is still lit correctly (Task 1 changed its lighting expression).
4. Asteroid belt vs Kuiper belt still have distinct tints — rusty rock inside Neptune, icy blue outside (Task 1 changed the test that separates them).
5. Shadows still land correctly (solar eclipse mode on the Moon is the sharpest test).
6. Clicking bodies and nametags still selects the right one at both scales.
7. Visual scale still looks exactly as before.

List these for the user with the build and harness results.
