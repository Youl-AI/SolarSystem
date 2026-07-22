#pragma once

#include <string>

// 천체의 제원을 사람이 읽는 문자열로 돌려준다. 정보 패널이 쓴다.
// 이름을 모르면 빈 문자열이 아니라 기존과 동일한 기본 문구를 돌려준다.
std::string getCelestialInfo(const std::string &name);
