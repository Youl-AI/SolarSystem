#!/bin/bash
# 색상 텍스처를 BC7 DDS로 굽는다(밉맵 포함).
#
# 노말맵은 제외한다 — BC7이 더하는 법선 오차가 수성 0.78도, 달 2.26도로,
# 8비트 저장 자체의 오차(0.135도)는 물론 앞서 기각한 BC5(0.18~0.32도)보다도 크다.
# 블록당 대표색 2개를 잇는 직선으로는 구면 위의 법선을 근사할 수 없기 때문이다.
#
# sRGB 텍스처에는 반드시 -srgbi(입력이 이미 sRGB임)를 준다.
#
# texconv는 출력 포맷이 _SRGB면 기본으로 -srgbo처럼 굴어서, 입력을 선형으로 여기고
# sRGB로 인코딩해 저장한다. JPEG/PNG 바이트는 이미 sRGB이므로 이건 이중 인코딩이다.
# 원본 128이 파일에 188로 저장되고, GPU가 다시 sRGB 디코드하면 0.502가 나온다
# (올바른 값은 0.216). 중간톤이 2.3배 밝아지고 하이라이트는 255 그대로라, 화면이
# 물빠진 채 밝아진다. -srgbi가 그 변환을 상쇄해 블록 바이트가 원본과 완전히 같아진다
# (계단 이미지로 검증: BC7_UNORM 페이로드와 21904바이트 전부 일치).
#
# 확인 방법: 같은 이미지를 -f BC7_UNORM으로도 굽고 헤더 148바이트 이후를 바이트 비교한다.
# 디코더로 값을 비교하면 안 된다 — 디코드 경로가 역변환을 걸어 차이를 지워버린다.
set -e
# 저장소 루트. 특정 PC의 절대 경로가 박혀 있어 다른 곳에서는 동작하지 않았다.
R="$(cd "$(dirname "$0")/.." && pwd)"
TEXCONV="$(dirname "$0")/texconv.exe"

# diffuse / night / clouds 슬롯 = sRGB
SRGB="
textures/sun.jpg
textures/Milkyway.png
textures/planets/8k_mercury.jpg
textures/planets/8k_venus_surface.jpg
textures/planets/venus_atmosphere.jpg
textures/planets/earth_diffuse.png
textures/planets/earth_night.png
textures/planets/earth_clouds.png
textures/planets/8k_mars.jpg
textures/planets/jupiter.png
textures/planets/8k_saturn.jpg
textures/planets/8k_saturn_ring_alpha.png
textures/planets/uranus.jpg
textures/planets/4k_neptune.jpg
textures/moons/8k_moon.jpg
textures/moons/4k_io.jpg
textures/moons/io_glow.png
textures/moons/4k_europa.jpg
textures/moons/4k_ganymede.jpg
textures/moons/4k_callisto.jpg
textures/moons/4k_titan.jpg
textures/moons/titan_atmosphere.jpg
textures/asteroids/ceres.jpg
textures/asteroids/pluto.jpg
textures/asteroids/haumea.jpg
textures/asteroids/makemake.jpg
textures/asteroids/eris.jpg
textures/asteroids/Bennu.jpg
textures/asteroids/Gaspra.jpg
textures/asteroids/Ida.jpg
textures/asteroids/Itokawa.jpg
"
# specular 슬롯 = 선형(UNORM)
LINEAR="
textures/planets/8k_earth_specular.png
"
# 노말맵 = BC5(2채널). z는 셰이더가 sqrt(1-x^2-y^2)로 복원한다.
# -dx10을 반드시 준다. 없으면 texconv가 구식 FourCC "BC5U" 헤더로 저장하는데,
# 그러면 DX10 확장 헤더가 없어 데이터 시작 위치가 148이 아니라 128이 되고 로더가 거른다.
# 달은 제외한다 — 블록 압축 오차가 크레이터 테두리에 몰려(평탄부 0.70도 대 테두리 2.61도)
# 하필 가장 눈에 띄는 곳을 뭉갠다. 나머지는 0.16~0.70도라 문제되지 않는다.
NORMAL="
textures/planets/mercury_normal.png
textures/planets/venus_normal.png
textures/planets/earth_normal.png
textures/planets/mars_normal.png
"

conv() {
  local fmt="$1"; local extra="$2"; shift 2
  for rel in $@; do
    src="$R/$rel"; dir="$(dirname "$src")"; base="$(basename "${rel%.*}")"
    [ -f "$src" ] || { echo "  건너뜀(없음): $rel"; continue; }
    "$TEXCONV" -f "$fmt" $extra -y -o "$dir" -nologo "$src" > /dev/null 2>&1
    # texconv는 원본 확장자를 .DDS로 바꿔 저장한다
    [ -f "$dir/$base.DDS" ] && mv -f "$dir/$base.DDS" "$dir/$base.dds"
    printf "  %-46s %7.1f MB\n" "$base.dds" "$(stat -c %s "$dir/$base.dds" | awk '{print $1/1048576}')"
  done
}
echo "=== sRGB (BC7_UNORM_SRGB)"
conv BC7_UNORM_SRGB "-srgbi" $SRGB
echo "=== 선형 (BC7_UNORM)"
conv BC7_UNORM "" $LINEAR
echo "=== 노말맵 (BC5_UNORM)"
conv BC5_UNORM "-dx10" $NORMAL
