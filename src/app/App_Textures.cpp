#include "SolarSystemApp.hpp"

void SolarSystemApp::createColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, VkImage &image, VkDeviceMemory &imageMemory, VkImageView &imageView, VkFormat format) {
    uint8_t pixels[4] = {r, g, b, a}; VkDeviceSize imageSize = 4; VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
    void *data; vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data); memcpy(data, pixels, static_cast<size_t>(imageSize)); vkUnmapMemory(device, stagingBufferMemory);
    createImage(1, 1, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, imageMemory);
    transitionImageLayout(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer, image, 1, 1);
    transitionImageLayout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr);
    imageView = createImageView(image, format, VK_IMAGE_ASPECT_COLOR_BIT);
}

// 블록 압축 DDS(BC7 색상, BC5 법선)를 읽어 압축된 블록을 그대로 GPU에 올린다.
//
// texconv가 내는 DDS는 헤더 124바이트 + DX10 확장 20바이트 뒤에 밉 레벨이 큰 것부터
// 차례로 붙어 있다. 블록 압축이라 한 레벨의 크기는 4로 올림한 블록 수 x 16바이트다.
// GPU가 그 포맷을 못 쓰면 VK_NULL_HANDLE을 돌려 호출자가 원본 이미지로 물러나게 한다.
VkImageView SolarSystemApp::loadDDS(const std::string &path, bool srgb) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return VK_NULL_HANDLE;
    std::vector<uint8_t> buf(static_cast<size_t>(f.tellg()));
    f.seekg(0); f.read(reinterpret_cast<char *>(buf.data()), buf.size());
    if (buf.size() < 148 || memcmp(buf.data(), "DDS ", 4) != 0) return VK_NULL_HANDLE;

    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, buf.data() + o, 4); return v; };
    uint32_t height = u32(12), width = u32(16), mips = u32(28);
    bool dx10 = memcmp(buf.data() + 84, "DX10", 4) == 0;
    if (!dx10) return VK_NULL_HANDLE;              // BC7/BC5는 DX10 확장 헤더로만 표현된다
    uint32_t dxgi = u32(128);
    if (mips == 0) mips = 1;

    // 색공간은 호출자가 정한다(DDS 태그가 아니라 슬롯의 용도가 기준이다). BC5는
    // 법선 전용이라 언제나 선형이다.
    VkFormat format;
    if (dxgi == 98 || dxgi == 99)                  // 98=BC7_UNORM, 99=BC7_UNORM_SRGB
        format = srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
    else if (dxgi == 83)                           // 83=BC5_UNORM
        format = VK_FORMAT_BC5_UNORM_BLOCK;
    else
        return VK_NULL_HANDLE;
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
    if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        static bool warned = false;
        if (!warned) { std::cerr << "이 GPU는 블록 압축 텍스처를 지원하지 않습니다. 원본을 씁니다.\n"; warned = true; }
        return VK_NULL_HANDLE;
    }

    const size_t dataOffset = 148;                  // 4 + 124 + 20
    std::vector<VkBufferImageCopy> regions;
    size_t offset = dataOffset;
    for (uint32_t m = 0; m < mips; ++m) {
        uint32_t w = std::max(1u, width >> m), h = std::max(1u, height >> m);
        size_t bytes = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * 16;
        if (offset + bytes > buf.size()) { mips = m; break; }   // 잘린 파일은 있는 데까지만
        VkBufferImageCopy r{};
        r.bufferOffset = offset - dataOffset;
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, 1};
        r.imageExtent = {w, h, 1};
        regions.push_back(r);
        offset += bytes;
    }
    if (regions.empty()) return VK_NULL_HANDLE;

    VkDeviceSize total = offset - dataOffset;
    VkBuffer staging; VkDeviceMemory stagingMem;
    createBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);
    void *dst; vkMapMemory(device, stagingMem, 0, total, 0, &dst);
    memcpy(dst, buf.data() + dataOffset, static_cast<size_t>(total));
    vkUnmapMemory(device, stagingMem);

    VkImage img; VkDeviceMemory mem;
    createImage(width, height, format, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img, mem, mips);
    transitionImageLayout(img, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, mips);
    VkCommandBuffer cb = beginSingleTimeCommands();
    vkCmdCopyBufferToImage(cb, staging, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());
    endSingleTimeCommands(cb);
    transitionImageLayout(img, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, mips);
    vkDestroyBuffer(device, staging, nullptr); vkFreeMemory(device, stagingMem, nullptr);

    VkImageView view = createImageView(img, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, mips);
    allImages.push_back(img); allMemories.push_back(mem); allViews.push_back(view);
    return view;
}

// BC7로 미리 압축해 둔 .dds가 옆에 있으면 그것을 쓴다.
//
// 색상 텍스처는 GPU에서 픽셀당 4바이트로 풀려 있어 VRAM의 대부분을 차지한다. BC7은
// 4x4 블록을 16바이트로 저장해 정확히 1/4로 줄이면서, 측정상 지금 쓰는 JPEG보다
// 손실이 작다(달 8K: BC7 44.3dB 대 JPEG q92 40.4dB). 노말맵은 제외했다 — 법선은
// 블록당 대표색 2개를 잇는 직선으로 근사되지 않아 오차가 커진다.
//
// 압축 포맷은 blit으로 밉맵을 만들 수 없으므로 texconv가 구울 때 함께 넣어 둔다.
std::string SolarSystemApp::ddsPathFor(const std::string &path) {
    size_t dot = path.find_last_of('.');
    return (dot == std::string::npos) ? path + ".dds" : path.substr(0, dot) + ".dds";
}

VkImageView SolarSystemApp::loadTexture(const std::string &path, VkFormat format) {
    if (path.empty()) return (format == VK_FORMAT_R8G8B8A8_UNORM) ? viewDummyFlatNormal : viewDummyBlack;

    std::string dds = ddsPathFor(path);
    if (std::ifstream(dds, std::ios::binary).good()) {
        VkImageView v = loadDDS(dds, format == VK_FORMAT_R8G8B8A8_SRGB);
        if (v != VK_NULL_HANDLE) return v;
        std::cerr << "DDS 로드 실패, 원본으로 대체: " << dds << "\n";
    }

    VkImage img; VkDeviceMemory mem; VkImageView view; int texWidth, texHeight, texChannels;
    stbi_uc *pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) { std::cerr << "텍스처 없음, 더미 사용: " << path << std::endl; return viewDummyBlack; }
    VkDeviceSize imageSize = texWidth * texHeight * 4; VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
    void *data; vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data); memcpy(data, pixels, static_cast<size_t>(imageSize)); vkUnmapMemory(device, stagingBufferMemory);
    stbi_image_free(pixels);
    // 밉맵 체인. 8K 텍스처가 화면에서 수백 픽셀로 축소되면 밉맵 없이는 텍스처 캐시가
    // 거의 매번 빗나가고 축소 지글거림(shimmering)도 생긴다. blit이 안 되는 포맷이면
    // 레벨 1개로 물러난다(기존 동작과 동일).
    uint32_t mipLevels = 1;
    if (supportsLinearBlit(format))
        mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mipLevels > 1) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // blit 소스로도 쓰인다
    createImage(texWidth, texHeight, format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img, mem, mipLevels);
    transitionImageLayout(img, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, mipLevels);
    copyBufferToImage(stagingBuffer, img, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    if (mipLevels > 1)
        generateMipmaps(img, texWidth, texHeight, mipLevels); // 전 레벨을 SHADER_READ_ONLY로 남긴다
    else
        transitionImageLayout(img, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr);
    view = createImageView(img, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 1, mipLevels);
    allImages.push_back(img); allMemories.push_back(mem); allViews.push_back(view); return view;
}

// 모노크롬 UI 아이콘 로더. 형태는 PNG의 알파 채널이 정의하고(배경/중앙은 alpha=0으로 투명),
// 색은 지정한 회색으로 통일한다. 원본 획이 검정이라 ImGui tint(곱셈)로는 밝게 만들 수 없어
// RGB를 직접 덮어쓴다. 512px 원본을 40px 버튼에 밉맵 없이 축소하면 얇은 획이 계단현상을
// 일으키므로, CPU 박스 평균으로 64x64로 먼저 줄여 알파 기반 안티앨리어싱을 확보한다.
VkImageView SolarSystemApp::loadGrayIcon(const std::string &path, uint8_t gray) {
    int sw, sh, ch;
    stbi_uc *src = stbi_load(path.c_str(), &sw, &sh, &ch, STBI_rgb_alpha);
    if (!src) { std::cerr << "아이콘 없음: " << path << "\n"; return VK_NULL_HANDLE; }

    const int tw = 64, th = 64;
    std::vector<uint8_t> dst(tw * th * 4);
    for (int ty = 0; ty < th; ++ty) {
        for (int tx = 0; tx < tw; ++tx) {
            int x0 = tx * sw / tw, x1 = (tx + 1) * sw / tw;
            int y0 = ty * sh / th, y1 = (ty + 1) * sh / th;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            uint32_t aSum = 0, n = 0;
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) { aSum += src[(y * sw + x) * 4 + 3]; ++n; }
            int di = (ty * tw + tx) * 4;
            dst[di + 0] = gray; dst[di + 1] = gray; dst[di + 2] = gray;
            dst[di + 3] = (uint8_t)(aSum / n);   // 다운스케일된 알파 = 부드러운 회색 기어 형태
        }
    }
    stbi_image_free(src);

    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;   // 평면 UI 회색이라 sRGB 변환 없이 그대로 표시
    VkDeviceSize imageSize = (VkDeviceSize)tw * th * 4; VkBuffer sb; VkDeviceMemory sbm;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sb, sbm);
    void *data; vkMapMemory(device, sbm, 0, imageSize, 0, &data); memcpy(data, dst.data(), (size_t)imageSize); vkUnmapMemory(device, sbm);
    VkImage img; VkDeviceMemory mem;
    createImage(tw, th, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, img, mem);
    transitionImageLayout(img, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(sb, img, (uint32_t)tw, (uint32_t)th);
    transitionImageLayout(img, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkDestroyBuffer(device, sb, nullptr); vkFreeMemory(device, sbm, nullptr);
    VkImageView view = createImageView(img, format, VK_IMAGE_ASPECT_COLOR_BIT);
    allImages.push_back(img); allMemories.push_back(mem); allViews.push_back(view); return view;
}

// BC6H로 미리 구운 큐브맵 DDS를 읽는다. 성공하면 true.
//
// BC6H는 HDR 전용 블록 압축이라 half 4채널을 픽셀당 1바이트로 담는다. 이게 없으면
// 해상도 업그레이드가 성립하지 않는다 — 4096^2 x 6면을 fp16으로 올리면 805 MiB인데,
// BC6H로는 밉맵까지 다 넣고도 128 MiB다(2048^2 fp16 무압축 192 MiB보다도 작다).
bool SolarSystemApp::loadCubeDDS(const std::string &path, VkImage &outImg, VkDeviceMemory &outMem, VkImageView &outView) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::vector<uint8_t> buf(static_cast<size_t>(f.tellg()));
    f.seekg(0); f.read(reinterpret_cast<char *>(buf.data()), buf.size());
    if (buf.size() < 148 || memcmp(buf.data(), "DDS ", 4) != 0) return false;

    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, buf.data() + o, 4); return v; };
    uint32_t height = u32(12), width = u32(16), mips = u32(28);
    if (memcmp(buf.data() + 84, "DX10", 4) != 0) return false;
    uint32_t dxgi = u32(128), misc = u32(136);
    if (mips == 0) mips = 1;
    if (!(misc & 0x4)) { std::cerr << "스카이박스 DDS가 큐브맵이 아닙니다: " << path << "\n"; return false; }

    VkFormat format;
    if (dxgi == 95)      format = VK_FORMAT_BC6H_UFLOAT_BLOCK;   // 95 = BC6H_UF16
    else if (dxgi == 96) format = VK_FORMAT_BC6H_SFLOAT_BLOCK;   // 96 = BC6H_SF16
    else { std::cerr << "스카이박스 DDS의 포맷을 모릅니다(dxgi=" << dxgi << ")\n"; return false; }

    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
    if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
        std::cerr << "이 GPU는 BC6H를 지원하지 않습니다. EXR 스카이박스로 돌아갑니다.\n";
        return false;
    }

    // DDS 큐브맵은 면이 바깥 루프다 — 면 0의 밉 전부, 면 1의 밉 전부, ... 순서로 놓인다.
    const size_t dataOffset = 148;
    std::vector<VkBufferImageCopy> regions;
    size_t offset = dataOffset;
    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t m = 0; m < mips; ++m) {
            uint32_t w = std::max(1u, width >> m), h = std::max(1u, height >> m);
            size_t bytes = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * 16;
            if (offset + bytes > buf.size()) { std::cerr << "스카이박스 DDS가 잘렸습니다\n"; return false; }
            VkBufferImageCopy r{};
            r.bufferOffset = offset - dataOffset;
            r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, face, 1};
            r.imageExtent = {w, h, 1};
            regions.push_back(r);
            offset += bytes;
        }
    }

    VkDeviceSize total = offset - dataOffset;
    VkBuffer staging; VkDeviceMemory stagingMem;
    createBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);
    void *dst; vkMapMemory(device, stagingMem, 0, total, 0, &dst);
    memcpy(dst, buf.data() + dataOffset, static_cast<size_t>(total));
    vkUnmapMemory(device, stagingMem);

    VkImageCreateInfo ii{}; ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent = {width, height, 1}; ii.mipLevels = mips; ii.arrayLayers = 6;
    ii.format = format; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(device, &ii, nullptr, &outImg);
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(device, outImg, &mr);
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size; ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &ai, nullptr, &outMem); vkBindImageMemory(device, outImg, outMem, 0);

    transitionImageLayout(outImg, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, mips);
    VkCommandBuffer cb = beginSingleTimeCommands();
    vkCmdCopyBufferToImage(cb, staging, outImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());
    endSingleTimeCommands(cb);
    transitionImageLayout(outImg, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6, mips);
    vkDestroyBuffer(device, staging, nullptr); vkFreeMemory(device, stagingMem, nullptr);

    outView = createImageView(outImg, format, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE, 6, mips);
    std::cout << "[skybox] " << path << "  " << width << "x" << height
              << " x6, 밉 " << mips << ", " << (total / 1048576) << " MiB\n";
    return true;
}

void SolarSystemApp::createCubeTextureImage() {
    // 실측 성도(NASA Deep Star Maps 2020)를 두 층으로 나눠 구워 둔 것이 있으면 쓴다.
    //
    //   skybox_stars = hiptyc   : 히파르코스/티코의 밝은 별만
    //   skybox_band  = milkyway : 은하수 확산광 + 어두운 Gaia 별 전부
    //
    // 나눠야 하는 이유: 별 개수를 실제 하늘(전천 9,100개, 6.5등급)에 맞추는 원시값
    // 문턱이 0.261인데 은하수 띠는 0.044라, 하나의 톤 곡선으로는 별 개수를 맞추는
    // 순간 은하수가 사라진다. 점광원과 확산광은 눈의 검출 문턱이 다른데(띠는 넓어서
    // 공간적으로 적분돼 보인다), 픽셀 단위 함수로는 그 차이를 표현할 수 없다.
    if (loadCubeDDS("textures/skybox_stars.dds", texSkybox, memSkybox, viewSkybox) &&
        loadCubeDDS("textures/skybox_band.dds", texSkyBand, memSkyBand, viewSkyBand))
        return;

    // 한쪽만 성공했으면 되돌린다. 두 층이 짝으로 있어야 셰이더 계수가 맞는다.
    if (viewSkybox != VK_NULL_HANDLE) {
        vkDestroyImageView(device, viewSkybox, nullptr); vkDestroyImage(device, texSkybox, nullptr); vkFreeMemory(device, memSkybox, nullptr);
        viewSkybox = VK_NULL_HANDLE; texSkybox = VK_NULL_HANDLE; memSkybox = VK_NULL_HANDLE;
    }

    // 🚀 확장자를 .exr로 설정합니다! (textures 폴더 안에 6장의 EXR 파일이 있어야 합니다)
    std::vector<std::string> cubeFaces = {
        "textures/right.exr", "textures/left.exr", 
        "textures/top.exr", "textures/bottom.exr", 
        "textures/front.exr", "textures/back.exr"
    };
    
    int texWidth = 0, texHeight = 0; 
    
    // 🚀 8비트 정수가 아닌 32비트 실수(float) 포인터 배열 사용
    float *pixels[6]; 
    const char* err = nullptr;

    for (int i = 0; i < 6; i++) { 
        int width, height;
        // 🚀 tinyexr 라이브러리를 사용하여 EXR 파일 로드
        int ret = LoadEXR(&pixels[i], &width, &height, cubeFaces[i].c_str(), &err);
        
        if (ret != TINYEXR_SUCCESS) {
            if (err) {
                std::string errorMsg = err;
                FreeEXRErrorMessage(err); // 에러 메시지 메모리 누수 방지
                throw std::runtime_error("EXR 로드 실패: " + cubeFaces[i] + " - " + errorMsg);
            }
            throw std::runtime_error("EXR 로드 실패: " + cubeFaces[i]);
        }
        
        // 6면의 해상도가 같아야 하므로 첫 번째 파일의 해상도를 기준으로 삼음
        if (i == 0) {
            texWidth = width;
            texHeight = height;
        }
    }
    
    // GPU에는 half(fp16) 4채널로 올린다. EXR 원본의 채널 타입이 이미 HALF이므로
    // fp32로 올리면 없는 정밀도를 채우려고 VRAM만 2배 쓴다(2048^2 x 6면 기준 402 MiB).
    // tinyexr의 LoadEXR은 무조건 float로 풀어 주므로 여기서 다시 half로 접는다.
    VkDeviceSize srcLayerFloats = static_cast<VkDeviceSize>(texWidth) * static_cast<VkDeviceSize>(texHeight) * 4ull;
    VkDeviceSize layerSize = srcLayerFloats * sizeof(uint16_t);
    VkDeviceSize imageSize = layerSize * 6;
    
    VkBuffer stagingBuffer; VkDeviceMemory stagingBufferMemory;
    createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
    void *data; 
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    
    // 진단: half로 접었다 편 값이 원본과 얼마나 다른지 잰다. EXR 원본의 채널 타입이
    // HALF이므로 이론상 오차가 0이어야 하고, 0이 아니면 어딘가 잘못된 것이다.
    double maxAbsErr = 0.0, maxOrig = 0.0; VkDeviceSize nonZeroDiff = 0;

    for (int i = 0; i < 6; i++) {
        // packHalf2x16은 float 두 개를 half 두 개로 접어 uint32 하나에 담는다.
        // (r,g)와 (b,a)를 각각 한 번씩 접으면 픽셀당 16바이트가 8바이트가 된다.
        uint32_t *dst = reinterpret_cast<uint32_t *>(static_cast<char *>(data) + layerSize * i);
        const float *src = pixels[i];
        for (VkDeviceSize p = 0; p < srcLayerFloats; p += 4) {
            dst[0] = glm::packHalf2x16(glm::vec2(src[p + 0], src[p + 1]));
            dst[1] = glm::packHalf2x16(glm::vec2(src[p + 2], src[p + 3]));
            if (profiler.devTools()) {   // 검증은 SOLAR_PROFILE=1일 때만 (2500만 픽셀을 한 번 더 훑는다)
                glm::vec2 rg = glm::unpackHalf2x16(dst[0]), ba = glm::unpackHalf2x16(dst[1]);
                float back[4] = {rg.x, rg.y, ba.x, ba.y};
                for (int c = 0; c < 4; ++c) {
                    double d = std::fabs((double)back[c] - (double)src[p + c]);
                    if (d > 0.0) ++nonZeroDiff;
                    maxAbsErr = std::max(maxAbsErr, d);
                    maxOrig = std::max(maxOrig, (double)src[p + c]);
                }
            }
            dst += 2;
        }
        // 🚀 tinyexr은 내부적으로 malloc을 쓰기 때문에 반드시 free()로 메모리를 해제해야 합니다.
        free(pixels[i]);
    }
    if (profiler.devTools())
        std::cout << "[skybox] fp16 변환 검증: 원본 최대값=" << maxOrig
                  << "  최대 절대오차=" << maxAbsErr
                  << "  값이 달라진 채널 수=" << nonZeroDiff
                  << " / " << (srcLayerFloats * 6) << std::endl;
    vkUnmapMemory(device, stagingBufferMemory);
    
    // 🚀 [핵심] Vulkan 이미지 포맷을 실수형(SFLOAT)으로 변경하여 빛의 다이나믹 레인지를 보존합니다.
    // R16G16B16A16_SFLOAT은 Vulkan이 SAMPLED_IMAGE와 선형 필터링을 필수로 보장하는
    // 포맷이라 별도 지원 확인 없이 써도 된다.
    VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    // 진단: 예전에 쓰던 fp32가 이 장치에서 선형 필터링을 지원했는지 확인한다.
    // 32비트 실수 포맷은 SAMPLED_IMAGE_FILTER_LINEAR이 필수가 아니라, 지원하지 않는
    // 장치에서는 샘플러가 LINEAR로 설정돼 있어도 사실상 최근접으로 동작한다.
    // 그러면 fp16으로 바꾼 순간 필터링이 '처음으로' 켜지면서 별 모양이 달라 보인다.
    for (auto [fmt, name] : { std::pair<VkFormat, const char*>{VK_FORMAT_R32G32B32A32_SFLOAT, "R32G32B32A32_SFLOAT"},
                              {VK_FORMAT_R16G16B16A16_SFLOAT, "R16G16B16A16_SFLOAT"} }) {
        VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &fp);
        std::cout << "[skybox] " << name
                  << "  sampled=" << ((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? 1 : 0)
                  << "  linearFilter=" << ((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ? 1 : 0)
                  << "\n";
    }
    
    VkImageCreateInfo imageInfo{}; imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.extent.width = texWidth; imageInfo.extent.height = texHeight; imageInfo.extent.depth = 1; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 6; 
    imageInfo.format = hdrFormat; // 새로운 HDR 포맷 적용
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(device, &imageInfo, nullptr, &texSkybox);
    
    VkMemoryRequirements memReqs; vkGetImageMemoryRequirements(device, texSkybox, &memReqs); 
    VkMemoryAllocateInfo allocInfo{}; allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocInfo.allocationSize = memReqs.size; allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &allocInfo, nullptr, &memSkybox); vkBindImageMemory(device, texSkybox, memSkybox, 0);
    
    transitionImageLayout(texSkybox, hdrFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6);
    
    VkCommandBuffer cb = beginSingleTimeCommands();
    std::vector<VkBufferImageCopy> bufferCopyRegions;
    for (uint32_t face = 0; face < 6; face++) { 
        VkBufferImageCopy region{}; region.bufferOffset = layerSize * face; region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; region.imageSubresource.mipLevel = 0; region.imageSubresource.baseArrayLayer = face; region.imageSubresource.layerCount = 1; region.imageExtent = {(uint32_t)texWidth, (uint32_t)texHeight, 1}; 
        bufferCopyRegions.push_back(region); 
    }
    vkCmdCopyBufferToImage(cb, stagingBuffer, texSkybox, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bufferCopyRegions.size(), bufferCopyRegions.data());
    endSingleTimeCommands(cb);
    
    transitionImageLayout(texSkybox, hdrFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6);
    
    vkDestroyBuffer(device, stagingBuffer, nullptr); vkFreeMemory(device, stagingBufferMemory, nullptr);
    
    viewSkybox = createImageView(texSkybox, hdrFormat, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_CUBE, 6);
}
