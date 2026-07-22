#pragma once

#include <vulkan/vulkan.h>
#include <cstddef>

// 패스별 GPU 시간 계측 (개발자 전용).
//
// SOLAR_PROFILE=1 환경변수를 주고 실행할 때만 켜진다. 배포판을 그냥 실행하면
// 타임스탬프도 안 찍고 설정 창에 항목도 나오지 않는다 — 최종 사용자에게 보여 줄
// 정보가 아니고, 오히려 해석을 틀리기 쉬운 숫자다(%는 다른 패스가 싸져도 오른다).
//
// 개발 중에는 scripts/run_dev.bat으로 실행하면 된다.
class GpuProfiler {
public:
    // 슬롯 11개 = 구간 10개. 0시작 1컴퓨트 2소행성 3행성 4은하 5궤도선 6태양 7블러 8포스트 9끝
    static constexpr int SLOTS = 11;
    static const char *const NAMES[10];

    void init(VkPhysicalDevice phys, VkDevice dev);
    void destroy(VkDevice dev);

    bool devTools() const { return devTools_; }
    bool enabled()  const { return profiling_; }

    // 프레임 경계. CPU 프레임 시간을 누적하고 프레임 수를 센다.
    void beginFrame(double nowSeconds);
    // 커맨드 버퍼 맨 앞에서 쿼리 풀을 비운다.
    void resetPool(VkCommandBuffer cb);
    // 패스 경계마다 타임스탬프를 찍는다. 꺼져 있으면 아무 일도 안 한다.
    void mark(VkCommandBuffer cb, uint32_t slot);
    // 커맨드 기록에 걸린 CPU 시간(ms)을 누적한다.
    void addRecordMs(double ms);
    // 간접 그리기 버퍼의 LOD 바구니 스냅샷을 뜬다. 오버레이와 콘솔이 같이 쓴다.
    void snapshotDrawCmds(const void *mappedIndirectBuffer);
    // 직전 프레임의 타임스탬프를 읽어 누적한다(펜스 대기 후라 이미 준비돼 있다).
    void collect(VkDevice dev, size_t asteroidCount);

    // 오버레이용 지수 평활값. 매 프레임 원값을 쓰면 숫자가 읽을 수 없게 떨린다.
    double smoothMs(int i) const { return tsSmoothGpu_[i]; }
    const VkDrawIndexedIndirectCommand *drawCmds() const { return lastDrawCmds_; }

private:
    VkQueryPool tsPool_ = VK_NULL_HANDLE;
    float  tsPeriodNs_  = 0.0f;
    bool   devTools_    = false;
    bool   profiling_   = false;
    int    frameCount_  = 0;
    double accumCpuRecord_ = 0.0, accumCpuFrame_ = 0.0, lastFrameStart_ = 0.0;
    double tsAccumGpu_[10]  = {};
    double tsSmoothGpu_[10] = {};
    VkDrawIndexedIndirectCommand lastDrawCmds_[12] = {};
};
