# 소스 빌드 준비

이 저장소는 게임 데이터와 빌드 결과를 포함하지 않습니다. 아래는 Windows x64의 **새 clone**에서 의존성을 준비하는 순서입니다. 기존 `vendor/`의 수정 사항을 초기화하는 명령은 실행하지 마세요. 각 명령이 실패하면 그 단계에서 중단하고 원인을 확인합니다.

필요한 도구: Git, Node.js 24.13 이상·npm 11.6.2 이상, Python 3, Visual Studio 2022의 Desktop development with C++·Windows SDK·CMake. 지도 생성에는 본인이 설치한 ETS2 1.61.1.0과 보유 DLC가 필요합니다. 다른 게임 버전은 재검증 전까지 지원하지 않습니다.

## 외부 소스

프로젝트 루트에서 실행합니다. 의존성은 고정한 커밋으로 준비하며 최신 브랜치로 자동 갱신하지 않습니다.

```powershell
New-Item -ItemType Directory -Path vendor,data,output -Force

git clone https://github.com/ocornut/imgui.git vendor/imgui
git -C vendor/imgui checkout f5befd2d29e66809cd1110a152e375a7f1981f06

git clone https://github.com/TsudaKageyu/minhook.git vendor/minhook
git -C vendor/minhook checkout c3fcafdc10146beb5919319d0683e44e3c30d537

git clone https://github.com/ETS2LA/plugin.git vendor/ets2la-plugin
git -C vendor/ets2la-plugin checkout 7094b334f10b68343d0082f1ef4078669235106b
git -C vendor/ets2la-plugin submodule update --init --recursive
git -C vendor/ets2la-plugin apply --check ../../tools/ets2la-read-only.patch
git -C vendor/ets2la-plugin apply ../../tools/ets2la-read-only.patch

git clone https://github.com/truckermudgeon/maps.git vendor/maps
git -C vendor/maps checkout d56d0e3fb319230e84284f3029f8bda2c4b572a2
git -C vendor/maps submodule update --init --recursive
git -C vendor/maps apply --check ../../tools/parser-compat.patch
git -C vendor/maps apply ../../tools/parser-compat.patch
```

ETS2LA 패치는 이 저장소의 `native/ingame_route_publisher.hpp`·`native/ingame_route_protocol.hpp`를 사용하므로 위 폴더 구조를 유지합니다. fmt는 ETS2LA submodule 커밋 `9ff9c695db8aeadf70eb26498a0e1cdceeeb849a`로 준비됩니다. 각 구성 요소의 라이선스는 [THIRD_PARTY.md](../THIRD_PARTY.md)에 정리했습니다.

## SCS SDK 1.14

공식 배포본을 받아 해시를 확인한 뒤 압축을 풉니다. SDK 헤더가 `vendor/scs-sdk/include/scssdk_telemetry.h`에 있어야 합니다.

```powershell
Invoke-WebRequest https://download.eurotrucksimulator2.com/scs_sdk_1_14.zip -OutFile vendor/scs-sdk.zip
$taskSdkHash = (Get-FileHash vendor/scs-sdk.zip -Algorithm SHA256).Hash
if ($taskSdkHash -ne 'C6C1F7376B7324994D9F9C567F3C4141FBBF305B6BF803BC4CFEEF2437B2023A') {
    throw 'SDK archive differs from the verified 1.14 archive.'
}
Expand-Archive vendor/scs-sdk.zip -DestinationPath vendor/scs-sdk
```

## 지도 파서와 로컬 데이터

파서는 native addon을 사용합니다. Windows에서는 자동 postinstall의 심볼릭 링크 생성을 생략하고 parser를 직접 실행합니다. Python과 C++ 도구가 node-gyp에서 인식되어야 합니다.

```powershell
Push-Location vendor/maps
npm ci --ignore-scripts
npm run build --workspace packages/clis/parser
Pop-Location

powershell -NoProfile -File tools/update-map.ps1
node tools/compile-ingame-map.cjs
```

게임 설치 위치가 다르면 `update-map.ps1 -GamePath 'D:\Games\Euro Truck Simulator 2'`처럼 지정합니다. 지도 추출은 실행 중 게임의 경로나 세이브를 변경하지 않습니다. `data/raw/`, `data/map.json`, `data/ingame-map.bin`은 로컬에만 보관합니다.

## HUD 빌드·검사·설치

```powershell
powershell -NoProfile -File tools/build-signal-plugin.ps1
powershell -NoProfile -File tools/build-ingame.ps1
.\native\ingame-build\Release\ingame_render_host.exe .\native\ingame-build\Release\ets2_lane_ingame.dll --self-test
```

`build-ingame.ps1`은 내비게이션·GPS·신호 CTest도 실행합니다. 지도 진입 샘플 검사 때문에 로컬 지도 생성이 먼저 필요합니다. 미리보기는 `--far`로 먼 거리 안내, `--signal-demo`로 차선·신호 시연을 열 수 있습니다. 시연 값은 실제 ETS2 실행에서는 사용되지 않습니다.

게임을 완전히 종료한 다음 설치합니다. 기본 Steam 경로 외에는 `-GamePath`를 지정합니다. 설치 스크립트는 기존 HUD DLL·GPS producer DLL·지도를 `output/ingame-backup/`에 백업하고 교체합니다. 관리자 권한이 필요한 설치 폴더에서는 해당 권한으로 실행해야 합니다.

```powershell
powershell -NoProfile -File tools/install-ingame.ps1
```

별도 앱 자동실행·시작 프로그램·서비스·예약 작업은 만들지 않습니다. 게임이 SDK 플러그인을 로드하고 종료할 때 함께 종료합니다.

## 이전 Electron 버전

인게임 HUD에는 Electron이 필요하지 않습니다. 이전 외부 버전의 시연과 JS 검사를 사용하려는 경우 다음을 실행합니다. 이 버전의 경로는 별도 도시 선택 방식입니다.

```powershell
npm ci --ignore-scripts
node node_modules/electron/install.js
npm test
npm run demo
```

현재 작업 환경에서 빌드·자동 검사를 통과했으며 새 PC에서 위 전체 준비 순서를 다시 실행한 검증은 아직 하지 않았습니다. 실제 게임에서의 교차로·신호 정확도 검증 범위는 [README](../README.md)를 참고하세요.
