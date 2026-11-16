# GoW2 Asset Browser

God of War II **PS2** ISO와 개별 WAD를 탐색하는 ImHex 플러그인입니다.
`gow1_browser`와 같은 UI를 사용하며 별도의 뷰·Provider ID로 함께 로드할 수 있습니다.
폴더·빌드 타깃·결과물 이름은 각각 `gow2_browser`, `gow2_browser`, `gow2_browser.hexplug`입니다.

## 사용

**Containers (Source)**의 최상위는 확장자 폴더입니다. 예: `.WAD → 컨테이너 → 종류 → 에셋`.
확장자는 대소문자를 구분하지 않고 묶으며, 확장자가 없는 파일은 `(No extension)`에 표시합니다.
각 폴더의 개수는 컨테이너 출처 수이며 반복된 TOC 사본도 유지합니다.

1. **View → GoW2 Asset Browser**를 엽니다. 한국어 이름은 **GoW2 에셋 브라우저**입니다. 파일을 열기 전에도 사용할 수 있습니다.
2. `Browse ISO / WAD...`로 GoW2 ISO 또는 WAD를 선택하거나 경로를 입력하고 `Open`을 누릅니다.
3. **Assets**에서 종류 → 이름 → 출처를 펼칩니다. 같은 이름의 에셋도 TOC 엔트리·위치 슬롯·태그 번호로 구분합니다.
4. 더블클릭 또는 `Open selected`로 payload가 주소 `0`에서 시작하는 읽기 전용 헥스 탭을 엽니다.
5. `Apply pattern on open`으로 내장 패턴을 실행하고 Pattern Editor / Pattern Data를 표시합니다.
   이미 열린 탭은 재사용하며 편집한 패턴을 유지합니다. `Apply built-in pattern to current tab`으로 내장 패턴을 다시 적용합니다.
6. 사용자 `.hexpat`은 ImHex Pattern Editor에서 불러오거나 편집하여 실행할 수 있습니다.

**Containers (Source)**는 각 TOC 사본을 유지합니다. `Group by kind`가 기본으로 켜져 있어
컨테이너 → Textures·Materials·Objects 등 종류 → 태그 순으로 표시하며 항목 수도 표시합니다.
체크를 끄면 원본 태그 순서로 돌아갑니다. 종류 이름이나 에셋 이름으로 검색할 수 있습니다.
제어 태그와 링크도 보존하며 오른쪽 클릭 메뉴로 32바이트 태그 헤더를 별도로 엽니다.
payload가 없는 링크는 헤더를 엽니다.

색인은 백그라운드에서 수행하고 완료된 컨테이너부터 표시합니다. `Cancel`은 이미 읽은 결과를 유지합니다.
한 컨테이너의 오류는 다른 파일의 색인을 중단시키지 않습니다. 미지원 WAD는 오류를 표시하고 원시 헥스로 엽니다.
다른 ISO를 열어도 기존 에셋 탭은 유효하며 ImHex 프로젝트에 원본 경로·구간을 저장하고 복원할 수 있습니다.

## GoW2 포맷과 지원 범위

- ISO9660 루트의 `GODOFWAR.TOC`와 `PART1.PAK` / `PART2.PAK`를 사용합니다.
- TOC는 `u32 엔트리 수 → 36바이트 엔트리 배열 → u32 위치 배열`입니다.
  엔트리는 `name[24], size, location_count, location_start`를 포함합니다.
- 인코딩 섹터가 10,000,000 미만이면 PART1, 이상이면 그 값을 뺀 뒤 PART2를 참조합니다.
  바이트 위치는 해당 PAK 시작 + 섹터 × 2048입니다. 반복·중첩 위치 슬롯도 출처별로 유지합니다.
- WAD는 32바이트 태그와 16바이트 정렬을 사용합니다.
  태그 **0**의 크기 필드는 엔티티 개수이며 payload는 없습니다.
  태그 **1**은 리소스, **2/3**은 그룹 시작/끝, **9**는 이름 붙은 원시 레코드입니다.
- 에셋 분류는 GoW2 타입 번호를 사용합니다. 예를 들어 `0x10001`은 Objects,
  `0x30001`은 Instances, `0x20001`은 Reference nodes, `0x11`은 Particles입니다.
- 내장 패턴은 TOC·WAD·태그 헤더와 **TXR, GFX 헤더, MAT 헤더/레이어, 인스턴스/참조 노드 배치 prefix, 카메라/전환 링크**를 해석합니다.
  다른 리소스는 타입 ID와 원시 바이트를 표시합니다. 모델·애니메이션 상세 디코더 및 이미지·3D 미리보기는 포함하지 않습니다.
- ISO 전체나 에셋 payload를 색인 중 복사하지 않고 64 KiB 파일 읽기 캐시를 공유합니다.
  메타데이터 16 MiB, TOC 위치 100,000개, WAD당 태그 100,000개, 전체 태그 2,000,000개로 제한합니다.
- 2048바이트 섹터만 지원합니다. ISO9660 확장 속성·인터리브·다중 extent 및 raw 2352바이트 BIN은 지원하지 않습니다.
- GoW1의 TOC·WAD는 이 브라우저 대신 `gow1_browser`로 엽니다. 게임 수정·리패킹은 제공하지 않습니다.

## 빌드와 검증

플러그인은 `plugins/*/CMakeLists.txt` 자동 발견으로 포함됩니다. 플러그인 허용 목록을 지정했다면 `gow2_browser`도 포함합니다.

```powershell
cmake --preset windows-local
cmake --build --preset windows-local --target gow2_browser
```

이 PC의 결과물은 `build/vs2022-relwithdebinfo/plugins/gow2_browser.hexplug`입니다.
같은 빌드의 `imhex-gui.exe`에서 사용합니다. 다른 ImHex 버전에는 해당 버전으로 다시 빌드합니다.

```powershell
cmake --preset windows-local -DGOW2_BROWSER_BUILD_CHECKS=ON
cmake --build --preset windows-local --target gow2_browser_checks gow2_pattern_checks gow2_integration_checks
$buildPath = (Resolve-Path ./build/vs2022-relwithdebinfo).Path
$env:PATH = "$buildPath;$buildPath/plugins;$buildPath/vcpkg_installed/x64-windows-msvc/bin;$env:PATH"
& "$buildPath/gow2_browser_checks.exe"
& "$buildPath/gow2_pattern_checks.exe"
& "$buildPath/gow2_integration_checks.exe"
# gow1_browser도 빌드에 포함되어 있을 때 제공됩니다.
cmake --build --preset windows-local --target gow_browser_coexistence_checks
& "$buildPath/gow_browser_coexistence_checks.exe" "$buildPath/plugins"
```

파서 검증은 TOC 반복·중첩 슬롯, PART2 경계·누락·범위, 개수 오버플로, ISO 양 endian 필드,
WAD 그룹·정렬·취소·게임 구분을 확인합니다. 패턴 검증은 실제 Pattern Language 런타임을 사용합니다.
통합 검증은 시작 화면 메뉴, 종류별 트리, Provider, 패턴 이벤트, 탭 재사용과 설정 복원을 확인합니다.
동시 로딩 검증은 실제 GoW1·GoW2 플러그인을 함께 초기화하고 두 메뉴·뷰·Provider가 독립적인지 검사합니다.

실제 ISO는 아래처럼 명시했을 때만 읽습니다. 게임 바이트는 저장소에 포함하지 않습니다.

```powershell
& "$buildPath/gow2_browser_checks.exe" --iso 'path/to/GoW2.iso'
& "$buildPath/gow2_pattern_checks.exe" --iso 'path/to/GoW2.iso'
```

파서 검증의 `--iso`는 디코딩하지 못한 WAD가 있으면 오류를 집계하고 종료 코드 1을 반환합니다.
패턴 표본 검증은 해당 WAD를 건너뛰되 건너뛴 개수와 이유를 출력합니다.

## 포맷 근거와 실제 표본

사용자가 지정한 로컬 `GOWR/gow-projects`의 `docs/formats/GOW2.md`,
`src/formats/gow2.py`, `gow2_wad.py`, `ps2_game.py` 및 리소스 파서의 레이아웃을 참고했습니다.
GoW1 플러그인의 UI·읽기 전용 Provider를 기반으로 GoW2 파서와 자체 포함 패턴을 작성했으며,
참고 프로젝트의 Python 코드·패턴·외부 도구는 배포하지 않습니다.

사용자 DVD5 표본에서 TOC 위치 **1,532개**, 이름·크기 조합 **913개**, WAD 사본 **650개**를 확인했습니다.
디코딩한 사본의 태그는 **737,779개**, 리소스 태그는 **439,936개**입니다.
중복 이름·크기를 묶어 첫 사본만 집계하면 WAD **292개**, 태그 **467,777개**, 리소스 태그 **277,483개**로 참고 문서와 일치합니다.

**`R_MCICON.WAD`의 4개 출처는 미지원 태그 구조로 진단됩니다.** 참고 프로젝트에도 같은 제한이 있으며 원본 헥스는 유지합니다.
패턴은 TOC·첫 유효 WAD와 **57개 타입의 리소스 표본 171개**에서 실행했습니다.
이는 모든 리소스의 상세 의미를 해석하거나 모든 payload를 전수 검증했다는 뜻은 아닙니다.
