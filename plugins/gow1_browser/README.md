# GoW1 Asset Browser

God of War I **PS2** ISO 및 개별 WAD를 탐색하는 ImHex 플러그인입니다.
플러그인과 패턴은 ImHex의 현재 체크아웃과 함께 빌드합니다.
플러그인 이름·폴더·빌드 타깃은 `gow1_browser`입니다. 기존 프로젝트와 작업 공간의 호환성을 위해 저장된 뷰·Provider ID는 유지합니다.

## 사용

**Containers (Source)**의 최상위는 확장자 폴더입니다. 예: `.WAD → 컨테이너 → 종류 → 에셋`.
확장자는 대소문자를 구분하지 않고 묶으며, 확장자가 없는 파일은 `(No extension)`에 표시합니다.
각 폴더의 개수는 컨테이너 출처 수이며 반복된 TOC 사본도 유지합니다.

1. ImHex의 **View → GoW1 Asset Browser**를 엽니다. 한국어 UI에서는 **GoW1 에셋 브라우저**입니다.
2. `Browse ISO / WAD...`로 GoW1 ISO 또는 WAD를 선택하거나 경로를 입력하고 `Open`을 누릅니다.
3. **Assets**에서 종류 → 에셋 이름 → 출처를 펼칩니다. 같은 이름의 다른 출처와 모든 TOC 사본을 유지합니다.
4. 에셋을 더블클릭하거나 `Open selected`로 열면 에셋 payload의 첫 바이트가 주소 `0`인 읽기 전용 헥스 탭이 생깁니다.
5. `Apply pattern on open`이 켜져 있으면 내장 패턴을 실행하고 Pattern Editor / Pattern Data 창을 엽니다.
   기존 탭을 다시 열 때는 사용자가 수정한 패턴을 유지합니다. `Apply built-in pattern to current tab`으로 다시 적용할 수 있습니다.
6. 다른 `.hexpat`은 ImHex의 Pattern Editor에서 불러오거나 편집하고 실행합니다.

**Containers (Source)**에서는 `GODOFWAR.TOC`, 각 물리 파일 사본, 제어·패딩용 태그를 포함한 모든 WAD 태그를 검사할 수 있습니다.
기본으로 `Group by kind`가 켜져 있어 각 컨테이너 아래에 Textures·Materials·Objects 등의 종류 폴더와 항목 수를 표시합니다.
체크를 끄면 원래 태그 순서로 표시합니다. 종류별 보기에서는 종류 이름으로도 검색할 수 있습니다.
태그의 오른쪽 클릭 메뉴에서 32바이트 헤더를 별도로 열 수 있습니다. payload가 없는 링크는 헤더를 엽니다.
툴팁에 TOC 레코드 번호, 태그 번호, 원본 오프셋, 타입 ID, 부모 리소스를 표시합니다.

색인은 백그라운드에서 진행하며 완료된 컨테이너부터 탐색할 수 있습니다. `Cancel`은 이미 읽은 결과를 유지합니다.
손상된 WAD나 누락된 PAK의 오류는 컨테이너에 표시하고 다른 파일의 색인을 계속합니다.
열린 에셋 탭은 브라우저에서 다른 ISO를 열어도 유효하며 ImHex 프로젝트에 원본 경로와 구간을 저장·복원할 수 있습니다.

## 지원 범위

- 2048바이트 섹터 ISO9660 루트에서 `GODOFWAR.TOC` 및 TOC가 참조하는 `PARTn.PAK`를 찾습니다.
- GoW1 TOC는 24바이트 레코드 배열 뒤에 파일 끝의 12바이트 NUL 이름으로 종료합니다.
  중간에 이름 12바이트가 NUL이어도 뒤에 레코드 데이터가 있으면 보존합니다. 이름 없는 출처는 `<unnamed>`로 표시합니다.
- GoW1 WAD의 32바이트 태그, 16바이트 정렬, 그룹 소유권, 태그 24의 엔티티 개수를 처리합니다.
- 에셋 종류는 GoW1 원본 타입 ID와 충돌 형상 이름으로 구분합니다.
- ISO 전체나 에셋 payload를 색인 중 복사하지 않습니다. 파일별 64 KiB 읽기 캐시를 공유합니다.
- 메타데이터 16 MiB, TOC 위치 100,000개, WAD당 태그 100,000개, 전체 태그 2,000,000개의 제한이 있습니다.
- TOC·WAD·태그 헤더 패턴을 제공합니다. 리소스 패턴은 **TXR, GFX 헤더, MAT 헤더/레이어, GoW1 인스턴스, GoW1 카메라 헤더**를 해석합니다.
  그 외 리소스는 타입 ID와 원시 바이트를 표시합니다. 모델·애니메이션 등의 상세 디코더나 3D/이미지 미리보기는 이 버전에 포함하지 않습니다.
- GoW2 및 다른 작품의 포맷은 지원하지 않습니다. 게임 데이터 수정·리패킹은 제공하지 않습니다.
- 확장/인터리브/다중 extent ISO9660 레코드와 raw 2352바이트 섹터 이미지는 지원하지 않습니다.

## 빌드와 검증

이 저장소는 `plugins/*/CMakeLists.txt`를 자동 발견합니다. 플러그인 허용 목록을 지정한 빌드에서는 `gow1_browser`도 포함합니다.

```powershell
cmake --preset windows-local
cmake --build --preset windows-local --target gow1_browser
```

이 PC의 결과물은 `build/vs2022-relwithdebinfo/plugins/gow1_browser.hexplug`입니다.
해당 빌드의 `imhex-gui.exe`에서 사용합니다. 다른 ImHex 버전용 배포본에는 그 버전에 맞춰 다시 빌드해야 합니다.

```powershell
cmake --preset windows-local -DGOW1_BROWSER_BUILD_CHECKS=ON
cmake --build --preset windows-local --target gow1_browser_checks gow1_pattern_checks gow1_integration_checks
./build/vs2022-relwithdebinfo/gow1_browser_checks.exe
./build/vs2022-relwithdebinfo/gow1_pattern_checks.exe
# 통합 검증은 ImHex와 같은 DLL 검색 경로를 사용합니다.
$buildPath = (Resolve-Path ./build/vs2022-relwithdebinfo).Path
$env:PATH = "$buildPath;$buildPath/plugins;$buildPath/vcpkg_installed/x64-windows-msvc/bin;$env:PATH"
./build/vs2022-relwithdebinfo/gow1_integration_checks.exe
```

파서 검증에는 범위/오버플로, TOC 사본과 종료 레코드, ISO 양쪽 endian 필드, WAD 그룹·정렬·취소·게임 구분이 포함됩니다.
패턴 검증은 실제 Pattern Language 런타임을 사용하고 정상 및 손상된 합성 레코드를 실행합니다.
통합 검증은 브라우저 색인 → Provider → 패턴 이벤트, 탭 재사용, 원본 수명, 설정 복원과 ImGui 프레임 생성을 확인합니다.

실제 ISO는 명시적으로 지정했을 때만 읽습니다. 게임 데이터는 저장소나 테스트 fixture에 넣지 않습니다.

```powershell
./build/vs2022-relwithdebinfo/gow1_browser_checks.exe --iso 'path/to/GoW1.iso'
./build/vs2022-relwithdebinfo/gow1_pattern_checks.exe --iso 'path/to/GoW1.iso'
```

파서만 별도 빌드할 수도 있습니다(C++23 컴파일러 필요).

```powershell
cmake -S plugins/gow1_browser -B build/gow1-parser-checks
cmake --build build/gow1-parser-checks --config Release
ctest --test-dir build/gow1-parser-checks -C Release --output-on-failure
```

## 포맷 근거

사용자가 지정한 로컬 `GOWR/gow-projects`의 `docs/formats/GOW1.md`,
`src/formats/gow2.py`, `gow2_wad.py`, `ps2_game.py` 및 각 리소스 파서에 정리된 레이아웃을 참고하여
이 플러그인의 C++ 파서와 자체 포함 패턴을 새로 작성했습니다. 해당 프로젝트의 Python 코드·패턴·외부 도구는 배포하지 않습니다.

사용자 DVD5 표본의 검증 결과: TOC 위치 1,669개, 이름·크기 조합 957개,
WAD 사본 537개에서 태그 567,544개와 리소스 태그 337,067개를 읽었으며 오류는 없었습니다.
TOC 패턴의 `records` 배열은 40,056바이트이며, 그 뒤의 12바이트 NUL 이름이 40,068바이트 파일을 끝냅니다.
중복 이름·크기를 묶어 첫 사본만 집계하면 WAD 217개, 태그 389,552개, 리소스 태그 234,204개로 참고 문서의 집계와 일치합니다.
패턴은 TOC·첫 WAD 및 54개 타입의 리소스 표본 160개에서 실행했습니다.
이는 모든 리소스의 상세 의미를 해석했다는 뜻은 아닙니다.
