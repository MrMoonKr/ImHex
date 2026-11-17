# GoW3 Asset Browser

God of War III Remastered **PS4**의 풀린 게임 폴더와 WAD를 탐색하는 ImHex 플러그인입니다. `gow-projects`의 GoW3 포맷 문서와 파서를 참고했습니다. PS3판은 지원 범위에 포함하지 않습니다.

## 사용법

1. ImHex를 다시 실행하고 **View → GoW3 Asset Browser**를 엽니다. 파일을 열기 전 시작 화면에서도 선택할 수 있습니다.
2. **Open game folder...**에서 설치 폴더, `game`, `EXEC` 또는 원하는 하위 폴더를 선택합니다. **File...**에서는 WAD나 DDS 하나를 선택할 수 있습니다.
3. **Containers (Source)**에서 확장자 → 파일 → 종류 순서로 펼칩니다. 파일을 펼치면 백그라운드에서 내부 태그를 읽습니다. **Group by kind**를 끄면 원래 태그 순서로 표시합니다.
4. 에셋을 더블 클릭하거나 선택 후 **Open selected**를 누르면 읽기 전용 헥스 탭이 열립니다. **Apply pattern on open**을 켜면 Pattern editor와 Pattern Data도 함께 열립니다.
5. 전체 이름 검색과 종류별 탐색은 **Index assets** 후 **Assets** 탭을 사용합니다. 같은 이름이 여러 WAD에 있으면 원본별로 구별합니다. 검색 대상은 읽어 둔 파일의 이름과 태그입니다.

설치 폴더나 `game`을 선택하면 기본 `game`과 형제 `_Update*/game` 폴더를 함께 나열하며 원본 경로를 유지합니다. 업데이트를 기본 파일에 덮어 합치지는 않습니다. `EXEC` 또는 개별 하위 폴더를 선택하면 그 경로만 탐색합니다.

태그의 우클릭 메뉴에서 페이로드 또는 실제 64바이트 헤더를 열 수 있습니다. 텍스처에는 **Open embedded DDS**도 있습니다. 크기가 0인 링크는 헤더를 엽니다. **Open file**은 원본 파일 전체, **Open container with pattern**은 전체 파일에 컨테이너 패턴을 적용합니다. 같은 에셋을 다시 열면 기존 탭을 사용하여 편집 중인 패턴을 보존합니다. 내장 패턴은 **Apply built-in pattern to current tab**으로 다시 적용할 수 있습니다.

## 포맷 및 패턴

- WAD: little endian, 64바이트 태그 헤더와 16바이트 정렬. 이름은 56바이트입니다. 엔티티 수 태그, 그룹 소유자와 부모 관계, 컨텍스트 청크, 링크를 구별합니다.
- Containers에는 제어 태그까지 표시합니다. Assets에는 컨텍스트를 제외한 타입 있는 리소스와 텍스트·내장 파일·원시 데이터·폰트 아틀라스를 표시합니다. 알 수 없는 타입은 숫자 ID로 구별합니다.
- WAD 패턴은 최상위 `tags` 배열을 생성합니다. 개별 리소스에는 텍스처/DDS, 재질 및 레이어, 메시 헤더 및 오프셋 표, 오브젝트 조인트 표 및 이름, 게임 오브젝트와 카메라 변환 패턴을 제공합니다. 그 외 타입은 타입 ID와 원시 페이로드를 표시합니다.
- 텍스처의 DDS는 페이로드 `0x4C`부터 시작합니다. DDS 헤더와 원시 표면 데이터를 표시하며 픽셀 디코딩이나 미리보기는 하지 않습니다. 메시·스켈레톤 패턴도 전체 3D 디코더가 아닙니다.
- 그 외 확장자는 원본 파일로 열 수 있습니다. 패키지 복호화와 PS3 포맷 해석은 제공하지 않습니다.

플러그인과 패턴은 파일을 수정하지 않습니다. 프로젝트에 저장된 에셋 탭은 원본 경로와 바이트 범위로 복원합니다. 원본 파일에 계속 접근할 수 있어야 합니다.

폴더 나열과 WAD 검사는 백그라운드 작업이며 취소할 수 있습니다. 파일 200,000개, WAD당 태그 500,000개, 세션 전체 태그 4,000,000개를 제한으로 둡니다. 파일별 오류는 컨테이너에 표시합니다. 전체 WAD를 메모리에 복사하지 않고 작은 읽기 캐시와 열린 에셋 탭의 공유 원본을 사용합니다.

원시 바이트 배열에는 `[[sealed]]`를 적용하여 강조 구간을 페이로드 범위로 생성합니다. 대용량 파일에서 바이트 수만큼 구간을 만들지 않습니다. ImHex 기본 호버 강조와 패턴 위치 이동도 평가 작업과 같은 런타임 잠금을 사용하여, 패턴 재평가 중에는 구간 트리를 조회하지 않습니다.

## 빌드와 검증

플러그인은 일반 플러그인 검색으로 빌드에 포함됩니다. 검사 실행 파일은 선택적으로 활성화합니다.

```powershell
cmake --preset windows-local -DGOW3_BROWSER_BUILD_CHECKS=ON
cmake --build --preset windows-local --target gow3_browser gow3_browser_checks gow3_pattern_checks gow3_integration_checks gow3_coexistence_checks

$buildPath = (Resolve-Path build/vs2022-relwithdebinfo).Path
$env:PATH = "$buildPath;$buildPath/plugins;$buildPath/vcpkg_installed/x64-windows-msvc/bin;$env:PATH"
& "$buildPath/gow3_browser_checks.exe"
& "$buildPath/gow3_pattern_checks.exe"
& "$buildPath/gow3_integration_checks.exe"
& "$buildPath/gow3_coexistence_checks.exe" "$buildPath/plugins"
& "$buildPath/imhex.exe" --validate-plugin "$buildPath/plugins/gow3_browser.hexplug"
```

공존 검사는 GoW1, GoW2, GoWR 플러그인이 함께 활성화된 구성에서 생성됩니다. 테스트 픽스처는 합성 데이터이며 게임 데이터를 포함하지 않습니다.

실제 풀린 PS4 기본판 `game/EXEC` 검증 결과:

- 파일 1,531개 중 WAD 413개, 태그 1,266,925개, 리소스 태그 796,950개, Assets 대상 454,865개: 파싱 오류 0개.
- WAD 패턴 2개 파일의 배열 수와 모든 태그 시작 오프셋을 네이티브 파서와 비교했습니다.
- 리소스 타입 62종의 표본 184개와 내장 DDS 표본 3개에서 패턴 실행을 검증했습니다. 전용 리소스 패턴은 각각 3개 표본을 포함합니다.
- 합성 정상/손상 입력, 범위·정렬·그룹 검사, 폴더/업데이트 발견, Provider 복원 및 탭 재사용, UI 생성, 네 플러그인 동시 초기화를 검사했습니다.

추가 실데이터 검사는 각각 `gow3_browser_checks.exe --audit <폴더>`와 `gow3_pattern_checks.exe --audit <폴더>`로 실행할 수 있습니다. 모든 리소스의 상세 구조를 검증했다는 의미는 아닙니다.

폴더·파일 열기 경로는 `gow3_integration_checks.exe --open <폴더 또는 WAD>`로 검사합니다. 실제 `game/EXEC` 폴더와 `BAS100.WAD`(182,902,352바이트, 태그 6,742개)에서 파일 전체/컨테이너 탭 열기, 패턴 평가, 강조 구간 조회, 탭 전환을 검증했습니다. 네이티브 파일 선택창 자체를 자동 조작하는 검사는 아닙니다.

호버 동시 접근 회귀 검사는 다음과 같이 실행합니다. 기본 패턴 편집기의 실제 등록 함수를 사용하여 평가 잠금 중 접근 차단과 재평가 50회 중 반복 호버 조회를 확인합니다.

```powershell
cmake --preset windows-local -DIMHEX_BUILTIN_BUILD_CHECKS=ON
cmake --build --preset windows-local --target builtin pattern_hover_checks
& "$buildPath/pattern_hover_checks.exe"
```
