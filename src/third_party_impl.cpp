// 헤더 온리 라이브러리들의 구현부를 담는 유일한 번역 단위.
//
// stb / tinyexr / tinyobjloader는 _IMPLEMENTATION을 정의한 곳에 함수 본문을 쏟아낸다.
// 예전에는 main.cpp가 유일한 .cpp라 거기 있어도 됐지만, 파일이 여러 개로 나뉘면
// 두 곳에서 정의되는 순간 링커가 중복 심볼로 죽는다. 그래서 여기 한 곳에 못 박는다.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 0       // miniz.h와 miniz.c를 찾지 마!
#define TINYEXR_USE_STB_ZLIB 1    // 대신 이미 있는 stb_image의 zlib 코드를 사용해!
#include "tinyexr.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
