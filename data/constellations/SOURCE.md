# 별자리 데이터 출처

이 폴더의 `stars.csv` · `lines.csv` · `names.csv`는 **d3-celestial**의 데이터를
변환해 만들었다.

## 소스

- **프로젝트:** d3-celestial (Celestial Map)
- **저자:** Olaf Frohn
- **URL:** https://github.com/ofrohn/d3-celestial
- **사용한 원본 파일:**
  - `data/constellations.lines.json` — 별자리 선(각 별자리의 [적경, 적위] 폴리라인)
  - `data/constellations.json` — 별자리 이름

## 라이선스

**BSD 2-Clause License** — 재배포·수정이 허용되며 카피레프트가 아니다.
저작권 고지를 유지하는 조건이 있어 아래에 원문을 남긴다.

```
Copyright (c) 2015, Olaf Frohn
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. ...
```

(전문: https://github.com/ofrohn/d3-celestial/blob/master/LICENSE)

## 변환 방법

각 별자리의 MultiLineString 폴리라인에서:
- 꼭짓점을 별자리별로 좌표(소수 4자리) 기준 중복 제거해 노드(`stars.csv`, 전역 정수 id)로 만들었다.
- 폴리라인의 연속한 두 꼭짓점을 간선(`lines.csv`)으로 이었다.
- 이름은 `constellations.json`의 `name` 필드(IAU 공식 라틴 명칭)를 아스키로 정규화해 썼다.
  Serpens는 두 조각(Caput/Cauda)을 한 약자 `Ser`로 합쳤다.

원본은 겉보기 등급을 담지 않으므로 `stars.csv`에 등급 열은 없다(렌더·피킹에 쓰지 않는다).

## README 크레딧 문구

> Constellation lines and names: d3-celestial by Olaf Frohn (BSD-2-Clause).
