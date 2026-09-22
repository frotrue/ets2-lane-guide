# 신호등 잔여 시간 조사

2026-09-22 기준, **공식 SCS Telemetry SDK와 정적 지도만으로는 신호 잔여 시간을 얻을 수 없습니다.** 별도 공개 프로젝트 ETS2LA에는 게임 내부의 현재 상태·잔여 시간을 읽는 구현이 있어, 해당 출력의 읽기 전용 수신기와 경로별 신호 매칭을 구현했습니다. Win32 모의 producer를 통한 통합 검증은 통과했으며 **실제 ETS2 1.61.1.0 교차로에서의 일치 여부는 아직 주행 검증 전**입니다. 조건이 충족되지 않으면 `state.trafficSignal`은 `null`입니다.

## 확인한 근거

- [공식 Telemetry SDK 페이지](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry)는 최신 안정 버전을 1.14로 안내하며 차량 텔레메트리를 제공합니다. 해당 페이지가 연결한 [공식 SDK 1.14 ZIP](https://download.eurotrucksimulator2.com/scs_sdk_1_14.zip)을 내려받아 `vendor/scs-sdk/include`의 20개 헤더와 SHA-256을 비교했습니다. 모두 일치했습니다. 헤더에는 교통신호의 ID, 점등 상태, 남은 시간, 위상 시작 이벤트가 없습니다.
- `vendor/scs-sdk/include/common/scssdk_telemetry_common_channels.h`의 `game.time`은 게임 내 **분 단위 정수**입니다. `local.scale`은 지도 시간 배율이며 신호 기준시각이 아닙니다.
- `vendor/scs-sdk/include/scssdk_telemetry_event.h`의 프레임 시각은 렌더링·물리 시뮬레이션용입니다. `simulation_time`은 일시 정지 중에도 변하고 `paused_simulation_time`은 멈춥니다. 타이머는 0으로 재시작할 수 있습니다. 어느 값에도 신호 주기의 기준시각과 연결된다는 계약이 없습니다.
- [공식 Prefab Locators 문서](https://modding.scssoft.com/wiki/Documentation/Tools/SCS_Blender_Tools/Locators/Prefab_Locators#Traffic_Semaphore)는 신호 프로필과 각 상태의 간격, 주기 지연, AI 차선 연결을 설명합니다. 이는 정적 설정이며 실행 중 위상이나 잔여 시간을 제공하지 않습니다. 프로필을 사용하는 신호는 프로필 값이 적용됩니다.

설치된 게임에서 파싱한 `data/raw/europe-version.txt`는 **1.61.1.0**입니다. `europe-prefabDescriptions.json`의 1,974개 프리팹 설명 중 488개에 총 3,589개 semaphore가 있습니다. 이 중 2,196개는 프로필 사용(type 0), 167개는 모델만 표시(type 1), 1,226개는 직접 설정된 교통신호(type 2/3/4)입니다. 프리팹 인스턴스 20,593개 중 `showSemaphores`가 켜진 것은 1,869개입니다. semaphore는 교통신호 외 용도도 포함하므로 전체 개수를 교차로 수로 해석하면 안 됩니다.

예를 들어 `un_14004` 프리팹의 신호는 `intervals: [15, 2, 23, 2]`, `cycle: 0`, `profile: "2ph"`를 갖습니다. 이 숫자들은 지금 어느 단계인지 알려 주지 않습니다. PC 시각이나 게임 시각을 주기 길이로 나누면 그럴듯한 숫자는 나오지만 실제 신호와 일치한다는 근거가 없습니다. 프로필 사용 신호의 프리팹 간격은 최종 적용 간격이라고 가정할 수도 없습니다.

## 구현된 범위

추가된 인게임 버전에서는 `native/ingame_signals.cpp`의 C++ `SignalReader`가 이 수신·검증을 DLL 내부에서 처리합니다. 별도 `traffic_signal_reader.exe`를 실행하지 않습니다. 신호를 고르는 진행 경로는 [게임 GPS 경로](game-gps-sync.md)를 사용하므로 게임 지도에서 목적지를 설정하면 됩니다. 경로 연결을 확인할 수 없는 구간에서는 신호도 표시하지 않습니다. `native/ingame_signals_test.cpp`의 13개 테스트 묶음·115개 검증으로 타이머·그룹 매칭·만료·적색+황색 의미를 확인했습니다. 아래 JS와 외부 reader 설명은 이전 Electron 버전에 해당하며 두 버전 모두 같은 ETS2LA producer를 사용합니다.

ETS2LA의 [플러그인 소스](https://github.com/ETS2LA/plugin/blob/7094b334f10b68343d0082f1ef4078669235106b/src/processing/traffic.cpp)는 `traffic_light_t::state_time_remaining`와 `state`를 읽어 공유 메모리로 전달합니다. 이 경로는 화면의 색상을 인식하는 방식이 아니라 게임 내부 자료구조를 읽는 방식입니다. [ETS2LA 2026.9.5026 릴리스](https://github.com/ETS2LA/ETS2LA/releases/tag/v2026.9.5026)는 1.61 지원을 명시하며, 고정한 플러그인 커밋 `7094b334f10b68343d0082f1ef4078669235106b`의 빌드 설정도 게임 1.61.x / 플러그인 1.61.0입니다. [플러그인 라이선스는 MIT](https://github.com/ETS2LA/plugin/blob/7094b334f10b68343d0082f1ef4078669235106b/LICENSE.md)이며 CMake 3.15 이상, C++17, fmt 헤더 라이브러리와 포함된 SCS SDK를 사용합니다. **공식 SDK의 한계와 모든 구현 방식의 불가능은 다릅니다.**

`native/traffic_signal_reader.cpp`는 `OpenFileMapping(FILE_MAP_READ)`로 기존 `Local\ETS2LAPluginStatus`와 `Local\ETS2LASemaphore`만 읽습니다. 게임 프로세스를 열거나 내부 주소를 스캔하지 않고, 입력 공유 메모리를 열거나 쓰지 않습니다. 원본 ETS2LA producer 자체의 내부 메모리 접근과 이 수신기의 공유 메모리 읽기는 별개입니다. 매번 mapping 핸들을 닫아 producer 종료 후 오래된 mapping을 유지하지 않으며, 연속 두 번의 복사본이 다르면 샘플을 버립니다. 원본 형식에는 원자적 스냅샷 번호가 없어 이 비교가 완전한 원자성 보장을 뜻하지는 않습니다.

`src/traffic-signal-receiver.cjs`는 공개된 48바이트 × 40개 형식을 해석합니다. [출력 구조](https://github.com/ETS2LA/plugin/blob/7094b334f10b68343d0082f1ef4078669235106b/src/core.hpp)에는 신호 ID·위치·회전·종류·상태·현재 상태의 잔여 시간이 있지만 prefab UID와 생산 시각은 없습니다. 위치는 [공개 변환식](https://github.com/ETS2LA/plugin/blob/7094b334f10b68343d0082f1ef4078669235106b/src/prism/common.cpp)에 따라 셀 좌표 × 512를 더해 world XYZ로 변환합니다. 원본의 버전 문자열 변환 규칙에 따른 `1610`만 허용합니다.

내비게이터의 `guidance.signalTargets = [{id, positions: [{x,y,z}]}]`는 진행 경로의 제어 신호를 나타냅니다. 단순 최근접 신호는 선택하지 않습니다. 모든 목표 그룹에 대해 ID가 같고 지도 locator와 3차원 거리 2m 이내인 런타임 신호가 있어야 합니다. 동일 ID가 여러 위치에 있어도 처리하며, 매칭된 모든 신호의 상태가 같고 잔여 시간 전체 범위가 0.25초 이내여야 표시합니다. 목표 누락·서로 다른 위상·비정상 수치·꺼짐·점멸·차단기는 숨깁니다. 빨강+노랑은 빨강으로 표시하고 `적색·황색 · 전환까지`로 명시합니다. 숫자는 **현재 신호 상태가 바뀔 때까지**이며 빨강에서 출발할 수 있을 때까지의 시간으로 해석하면 안 됩니다.

최초 샘플만으로는 표시하지 않습니다. 연속 샘플의 타이머 감소 또는 정상적인 색 전이를 확인해야 하며, 750ms 넘게 새 샘플이 없거나 1초 넘게 타이머 진행이 없으면 숨깁니다. 게임 텔레메트리 연결 해제·배치 해제·일시 정지 때도 관측을 초기화합니다. 연결이 없어도 만들어 내는 실시간 카운트다운은 없습니다. `src/traffic-signals.cjs`의 합성 샘플은 명시적 `demo` 모드에서만 사용하고 `source: 'demo'`, `label: '시연 신호'`, 화면의 DEMO 표시를 유지합니다. 기존 차선 텔레메트리 DLL과 UDP 프로토콜은 변경하지 않았습니다.

렌더러 계약:

```js
state.trafficSignal = null; // 검증된 값 없음. 녹색 또는 0초로 해석하지 않음.
// 신호를 표시할 수 있을 때:
state.trafficSignal = {
  state: 'red' | 'green' | 'amber',
  remainingSeconds: 1, // 0 이상 정수, live는 관측한 현재 상태 잔여 시간을 올림
  source: 'live' | 'demo',
  label: '적색 · 전환까지' // demo는 '시연 신호'
};
```

## 실제 연동을 위한 다음 단계

로컬 producer DLL은 소스 빌드 후 게임 플러그인 폴더에 설치되었고 설치된 파일의 해시 일치까지 확인했습니다. 원본 `core.tick`의 `override_inputs()` 호출을 제거한 변경은 `tools/ets2la-read-only.patch`에 보존하며, 재현은 `tools/build-signal-plugin.ps1`과 `tools/install-signal-plugin.ps1`을 사용합니다. 현재 공개 구현의 1.61 지원 선언은 확인했지만, 수신기 단위·통합 검증을 실제 게임 호환성 검증으로 취급하면 안 됩니다. 실제 교차로에서 지도 신호 ID·위치가 producer 결과와 일치하는지, 선택한 진행 차선의 신호인지, 여러 번의 색 전환에서 표시 오차가 허용되는지 확인해야 합니다. 일시 정지·로드·이동·재접속·상충 신호에서 숫자가 사라지는지도 확인해야 합니다.

수신기 빌드는 `powershell -NoProfile -File tools/build-signal-reader.ps1`입니다. npm 네이티브 의존성은 추가하지 않습니다. `src/main.cjs`가 `native/traffic_signal_reader.exe`를 숨김 자식 프로세스로 시작하고 종료합니다. producer 또는 수신기가 없으면 신호 표시는 비어 있습니다.

검증 명령은 `npm test`와 `node tools/check-signal-bridge.cjs`입니다. 후자는 실제 게임이 아닌 Win32 모의 producer로 데이터 읽기→타이머 진행→멈춘 데이터 숨김→producer 종료 뒤 연결 해제를 확인합니다. 모의 producer는 게임 실행 중에는 거부하고, 기존 mapping이 있으면 `CreateNew`가 실패하므로 기존 데이터를 덮어쓰지 않습니다. 테스트 동안 새 오버레이를 실행하지 않아야 하며 테스트는 종료 시 생성한 mapping과 자식 프로세스를 정리합니다. 이러한 검증은 실제 게임에서의 신호 일치가 검증되었다는 의미가 아닙니다.
