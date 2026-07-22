#include "GpuProfiler.hpp"

#include <iostream>
#include <cstdlib>
#include <cstring>

const char *const GpuProfiler::NAMES[10] = {
    "compute", "asteroids", "bodies", "skybox", "galaxy+atmo",
    "orbits", "sun", "blur", "post", "tail"
};

void GpuProfiler::init(VkPhysicalDevice phys, VkDevice dev) {
    const char *env = std::getenv("SOLAR_PROFILE");
    devTools_ = (env && env[0] == '1');
    if (!devTools_) return;      // 배포 실행에서는 쿼리 풀조차 만들지 않는다

    VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(phys, &props);
    // 타임스탬프를 못 쓰는 큐가 있는 장치에서는 계측을 통째로 끈다.
    if (props.limits.timestampPeriod == 0.0f || props.limits.timestampComputeAndGraphics == VK_FALSE) {
        std::cout << "[profile] 이 장치는 타임스탬프 쿼리를 지원하지 않아 계측을 끕니다.\n";
        return;
    }
    tsPeriodNs_ = props.limits.timestampPeriod;
    VkQueryPoolCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = SLOTS;
    if (vkCreateQueryPool(dev, &qi, nullptr, &tsPool_) != VK_SUCCESS) return;
    profiling_ = true;
    std::cout << "[profile] on. timestampPeriod=" << tsPeriodNs_ << " ns/tick\n";
}

void GpuProfiler::destroy(VkDevice dev) {
    if (tsPool_ != VK_NULL_HANDLE) { vkDestroyQueryPool(dev, tsPool_, nullptr); tsPool_ = VK_NULL_HANDLE; }
}

void GpuProfiler::beginFrame(double nowSeconds) {
    if (!profiling_) return;
    if (lastFrameStart_ > 0.0) accumCpuFrame_ += (nowSeconds - lastFrameStart_) * 1000.0;
    lastFrameStart_ = nowSeconds;
    ++frameCount_;
}

void GpuProfiler::resetPool(VkCommandBuffer cb) {
    if (profiling_ && tsPool_ != VK_NULL_HANDLE) vkCmdResetQueryPool(cb, tsPool_, 0, SLOTS);
}

void GpuProfiler::mark(VkCommandBuffer cb, uint32_t slot) {
    if (!profiling_ || tsPool_ == VK_NULL_HANDLE) return;
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool_, slot);
}

void GpuProfiler::addRecordMs(double ms) { accumCpuRecord_ += ms; }

void GpuProfiler::snapshotDrawCmds(const void *mapped) {
    if (!profiling_ || !mapped) return;
    std::memcpy(lastDrawCmds_, mapped, 12 * sizeof(VkDrawIndexedIndirectCommand));
}

// 직전 프레임의 타임스탬프를 읽어 누적한다(펜스 대기 후라 이미 준비돼 있다).
void GpuProfiler::collect(VkDevice dev, size_t asteroidCount) {
    if (!profiling_ || tsPool_ == VK_NULL_HANDLE || frameCount_ == 0) return;
    uint64_t ts[SLOTS] = {};
    if (vkGetQueryPoolResults(dev, tsPool_, 0, SLOTS, sizeof(ts), ts, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    // 슬롯이 11개면 구간은 10개다. 예전엔 11번 돌아 tsAccumGpu[10](크기 10)과
    // ts[11]을 넘겨 읽고 썼다.
    for (int i = 0; i < 10; ++i) {
        double ms = (double)(ts[i + 1] - ts[i]) * tsPeriodNs_ / 1e6;
        tsAccumGpu_[i] += ms;
        // 오버레이는 매 프레임 갱신되므로 그대로 쓰면 숫자가 읽을 수 없게 떨린다.
        tsSmoothGpu_[i] += (ms - tsSmoothGpu_[i]) * 0.05;
    }

    if (devTools_ && frameCount_ % 120 == 0) {
        double n = 120.0;
        const char* const* names = NAMES;
        std::cout << "[profile] frames=" << frameCount_
                  << "  cpuFrame=" << (accumCpuFrame_ / n) << "ms"
                  << "  cpuRecord=" << (accumCpuRecord_ / n) << "ms  | GPU: ";
        double gpuTotal = 0;
        for (int i = 0; i < 10; ++i) { std::cout << names[i] << "=" << (tsAccumGpu_[i] / n) << " "; gpuTotal += tsAccumGpu_[i] / n; }
        std::cout << " gpuTotal=" << gpuTotal << "ms\n";

        // 실제로 어느 LOD에 몇 개가 들어갔는지(updateUniformBuffer에서 떠 둔 스냅샷).
        {
            const VkDrawIndexedIndirectCommand* cmds = lastDrawCmds_;
            uint32_t perLod[3] = {}, tris[3] = {};
            for (int lod = 0; lod < 3; ++lod)
                for (int type = 0; type < 4; ++type) {
                    const auto& c = cmds[lod * 4 + type];
                    perLod[lod] += c.instanceCount;
                    tris[lod] += c.instanceCount * (c.indexCount / 3);
                }
            std::cout << "[lodmix] LOD0=" << perLod[0] << " LOD1=" << perLod[1] << " LOD2=" << perLod[2]
                      << " (전체 " << (perLod[0] + perLod[1] + perLod[2]) << "/" << asteroidCount << ")"
                      << "  삼각형: LOD0=" << tris[0] << " LOD1=" << tris[1] << " LOD2=" << tris[2]
                      << " 합계=" << (tris[0] + tris[1] + tris[2]) << "\n";
        }
        std::cout << std::flush;
    }
    // 콘솔 출력 여부와 무관하게 120프레임마다 비운다. 예전엔 출력 블록 안에서만
    // 비워서, 콘솔을 끄면 누적값이 영원히 자랐다.
    if (frameCount_ % 120 == 0) {
        for (int i = 0; i < 10; ++i) tsAccumGpu_[i] = 0.0;
        accumCpuRecord_ = accumCpuFrame_ = 0.0;
    }
}
