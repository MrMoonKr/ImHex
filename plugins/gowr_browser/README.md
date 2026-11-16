# GoWR Asset Browser

God of War Ragnarök **PC** 게임 폴더와 컨테이너 파일을 탐색하는 ImHex 플러그인입니다.
폴더·빌드 타깃·결과물은 `gowr_browser`, 화면 이름은 **GoWR Asset Browser**입니다.
GoW1·GoW2 플러그인과 별도의 뷰·Provider ID를 사용합니다.

## 사용

1. ImHex를 재시작하고 **View → GoWR Asset Browser**를 엽니다. 파일을 열기 전에도 메뉴를 사용할 수 있습니다.
2. `Open game folder...`로 게임 루트 또는 `exec/wad/pc_le`, `exec/dc/pc_le` 같은 하위 폴더를 선택합니다.
   `File...`로 개별 컨테이너를 열거나 경로를 입력하고 `Open`을 눌러도 됩니다.
3. **Containers (Source)**에서 `확장자 → 상대 파일 경로 → 종류 → 청크`를 펼칩니다.
   `.TEXPACK.TOC`·`.LODPACK.TOC`는 별도 확장자 그룹으로 표시합니다.
4. 처음에는 파일 목록만 읽고, 컨테이너를 펼칠 때 백그라운드에서 내용을 읽습니다.
   **Index assets**로 모든 지원 컨테이너를 순차 색인할 수도 있습니다. **Cancel indexing**은 진행 중인 작업을 중단합니다.
5. **Assets**는 이미 색인된 청크를 `종류 → 이름 → 출처`로 묶습니다. 검색 대상도 이미 색인된 이름입니다.
6. 청크를 더블클릭하거나 `Open selected`로 열면 payload 시작이 주소 `0`인 읽기 전용 헥스 탭이 생깁니다.
   `Apply pattern on open`이 켜져 있으면 내장 패턴을 실행하고 Pattern Editor / Pattern Data 창을 표시합니다.
7. 태그 오른쪽 클릭 메뉴의 `Open entry record`, `Open debug data`, `Open temp data`로 다른 구간을 검사합니다.
   이미 열린 탭을 다시 선택할 때는 사용자가 수정한 패턴을 유지합니다.

**Open stored file**은 디스크상의 원본 바이트를 엽니다. **Open decoded container**는 LZ4 frame을 해제한 전체 컨테이너를 엽니다.
청크 오프셋은 **복원된 컨테이너 기준**이며 압축 파일의 seek 위치와 다릅니다. 툴팁과 Data Information에서 주소 공간을 구분합니다.

`Group by kind`를 끄면 컨테이너 안의 원래 엔트리 순서를 표시합니다.
다른 폴더를 열어도 기존 에셋 탭은 유효하며 ImHex 프로젝트에 경로·구간·압축 해제 여부를 저장하고 복원할 수 있습니다.

## 지원 범위

| 형식 | 현재 탐색/헥스 지원 |
| --- | --- |
| WAD / DCB / WYPDB | LZ4 frame 또는 직접 저장된 WTOC v2, 청크 본문·엔트리·debug·temp 구간 |
| TEXPACK / TEXPACK.TOC | v6 파일 헤더와 텍스처 키 레코드. 해시 이름으로 표시 |
| LODPACK / LODPACK.TOC | 그룹·블록 TOC 레코드. paired pack의 기하 데이터로 자동 이동하지 않음 |
| SHADERPACK | v11 셰이더 레코드와 외부 LZ4 frame. 내부 bytecode 영역이 비압축이면 bytecode 범위, 내부 압축이면 레코드 표시 |
| 기타 파일 | 원본 바이트 열기 |

WTOC는 0x40바이트 헤더와 0x90바이트 엔트리를 사용합니다. 엔트리의 메모리 타입·누적 arena 오프셋·정렬·load block 경계·패딩으로
본문 위치를 계산하며 debug는 arena 5, temp는 arena 8에 배치합니다. 리소스 종류는 id·server·client·이름으로 구분합니다.
청크 이름이 같아도 컨테이너 경로와 엔트리 번호가 다른 출처는 합치지 않습니다.

내장 패턴은 WTOC 헤더/엔트리, texture parm, DCB 버전, 각 pack의 헤더/레코드를 제공합니다.
그 밖의 청크는 원시 바이트로 표시하며 사용자 패턴은 ImHex Pattern Editor에서 불러올 수 있습니다.
DC 리플렉션 스키마 해석, 모델·애니메이션 상세 디코딩, 텍스처 재구성·이미지/3D 미리보기, pack 간 해시·이름 참조 연결은 제공하지 않습니다.
PS2 ISO·GoW1·GoW2 포맷은 각각의 전용 플러그인에서 엽니다. 게임 수정·리패킹은 제공하지 않습니다.

## 자원과 오류 처리

- LZ4는 공식 `lz4` 라이브러리로 해제하고 frame 체크섬을 검증합니다. 잘림·손상·뒤따르는 데이터·연결 frame은 거부합니다.
- 컨테이너 하나의 해제 결과는 최대 **512 MiB**입니다. 이미 열린 탭들은 소유 버퍼를 공유하며 서로 다른 컨테이너 탭을 열면 그만큼 메모리를 보관합니다.
- 파일 목록은 최대 100,000개, 컨테이너는 최대 1,000,000개 레코드, 세션 색인은 최대 2,000,000개 청크입니다.
  한도에 도달하면 더 작은 하위 폴더를 열어 탐색합니다.
- 파일은 필요한 구간만 읽으며 작은 읽기는 64 KiB 캐시를 사용합니다. 대형 비압축 pack 전체를 메모리로 복사하지 않습니다.
- 색인에 사용한 해제 버퍼는 최근 컨테이너 하나만 보관하고, 열린 탭이 소유한 버퍼는 탭이 닫힐 때까지 유지합니다.
- 손상되거나 미지원인 컨테이너는 해당 파일에 오류를 표시합니다. 다른 파일은 계속 탐색하며 원본 바이트도 열 수 있습니다.
- 게임 폴더에 캐시나 추출 파일을 쓰지 않습니다. 취소된 검사의 재시도는 소스 폴더/파일을 다시 열어 수행합니다.

## 빌드와 검증

이 플러그인은 LZ4 frame API를 사용합니다. `dist/vcpkg.json`에 `lz4` 의존성을 추가했으며 CMake의 `lz4::lz4` 타깃을 링크합니다.

```powershell
cmake --preset windows-local
cmake --build --preset windows-local --target gowr_browser
```

이 PC의 결과물은 `build/vs2022-relwithdebinfo/plugins/gowr_browser.hexplug`입니다. 같은 빌드의 ImHex에서 사용합니다.

```powershell
cmake --preset windows-local -DGOWR_BROWSER_BUILD_CHECKS=ON
cmake --build --preset windows-local --target gowr_browser_checks gowr_pattern_checks gowr_integration_checks
$buildPath = (Resolve-Path ./build/vs2022-relwithdebinfo).Path
$env:PATH = "$buildPath;$buildPath/plugins;$buildPath/vcpkg_installed/x64-windows-msvc/bin;$env:PATH"
& "$buildPath/gowr_browser_checks.exe"
& "$buildPath/gowr_pattern_checks.exe"
& "$buildPath/gowr_integration_checks.exe"
# GoW1·GoW2도 빌드에 포함했을 때:
cmake --build --preset windows-local --target gow_all_browser_checks
& "$buildPath/gow_all_browser_checks.exe" "$buildPath/plugins"
```

합성 검증은 여러 load block·누적 정렬·패딩·debug/temp·arena 범위, LZ4 저장/해제 주소 공간·잘림·체크섬,
pack 레코드, 시작 화면 메뉴·지연 색인·Provider·패턴 이벤트·탭 재사용·설정 복원을 확인합니다.
패턴 테스트는 실제 Pattern Language 런타임을 사용합니다. 동시 로딩 검증은 세 플러그인을 함께 초기화하고 메뉴·뷰·Provider를 확인합니다.

실제 파일은 아래처럼 명시한 경우에만 읽습니다. 각 확장자에서 최대 두 컨테이너를 선택하는 **표본 검사**이며 전체 payload 전수 검사가 아닙니다.

```powershell
& "$buildPath/gowr_browser_checks.exe" --audit 'path/to/GoWR'
& "$buildPath/gowr_pattern_checks.exe" --audit 'path/to/GoWR'
```

사용자 설치본에서 파일 **6,130개**를 발견했고, 8개 확장자 종류의 컨테이너 **16개**에서 청크/레코드 **43,960개**를 읽어 실패가 없었습니다.
같은 컨테이너들의 내장 패턴 및 선택한 payload 패턴도 실행했습니다.

## 포맷 근거

사용자가 지정한 로컬 `GOWR/gow-projects`의 `docs/formats/{WAD,LZ4,DCB,TEXTURE,LODPACK,SHADERPACK}.md`와
`src/formats/ragnarok/{wad,texpack,lodpack,shaderpack}.py`를 참고했습니다.
기존 GoW1·GoW2 플러그인의 UI를 기반으로 Ragnarök 파서와 자체 포함 패턴을 새로 작성했으며,
참고 프로젝트의 Python 코드·패턴·게임 데이터·외부 도구는 배포하지 않습니다.
