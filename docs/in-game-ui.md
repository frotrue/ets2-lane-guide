# 게임 내부 내비 UI와 외부 차선 안내

2026-09-22에 확인한 공개 기능 기준입니다.

게임 내부 모드로 GPS의 외형·배치와 차량 안 화면을 바꾸는 것은 가능합니다. SCS의 [accessory_addon_int_ui_data 문서](https://modding.scssoft.com/wiki/Documentation/Engine/Units/accessory_addon_int_ui_data)는 GPS·휴대전화 액세서리에 동적 UI를 표시하는 `ui_path`와 drawable texture를 설명합니다. 이 페이지는 작성 중 문서로 표시되어 있습니다.

다만 이 UI가 접근하는 값은 게임이 제공하는 [대시보드·GPS ID 목록](https://modding.scssoft.com/wiki/Documentation/Engine/Truck_Interior_Animations_and_IDs)에 정해져 있습니다. 확인한 목록에는 외부에서 계산한 추천 차로 배열이나 교통신호 잔여 시간을 전달하는 항목이 없습니다. 현재 앱의 Node.js 경로 계산·차로 판정을 `.scs`에 넣는 것만으로 게임이 실행하거나 표시해 주지는 않습니다.

처음 구현은 **위치를 수신하는 DLL + 경로 계산 앱 + 외부 안내창**이었습니다. 이후 사용자 요청으로 **C++ 차선 엔진 + DirectX 11 HUD + GPS·신호 수신기를 하나의 `ets2_lane_ingame.dll`로 옮겼습니다.** SCS Telemetry SDK가 위치·속도·일시정지를 전달하고, 작업 스레드가 게임 GPS 경로와 추출 지도의 도로 연결을 대조하며, MinHook과 Dear ImGui를 사용해 게임의 DX11 Present 단계에서 안내창을 그립니다. Node.js·Electron·외부 수신 EXE·자동 실행기는 사용하지 않습니다.

현재 버전의 목적지·경유지는 게임 지도에서 설정합니다. 별도의 도시 선택 창은 제거했습니다. F10은 위치 프리셋·크기·단축키를 보여 주는 읽기 전용 패널이며 마우스 메시지를 ImGui에 전달하지 않습니다. `NoMouse`·`NoMouseCursorChange`를 설정하고 모든 창에 `NoInputs`를 적용해 게임의 마우스 캡처·커서와 경쟁하지 않도록 했습니다. F8·Ctrl+F8로 6개 프리셋을 순환하고 Shift+F8로 크기를 바꿉니다. 위치는 매 프레임 화면 가장자리에 맞추므로 해상도가 바뀌어도 화면 안에 남습니다. 프리셋·크기는 `native-layout.ini`에 저장합니다.

이는 `.scs` UI 모드와 다른 **게임 프로세스 안의 네이티브 플러그인**입니다. GPS·신호 데이터의 원천인 읽기 전용 ETS2LA DLL은 여전히 함께 필요합니다. [경로 연동](game-gps-sync.md)은 공식 SDK 외의 게임 내부 자료구조에 의존합니다. SDK가 사용자 UI 그리기나 전체 GPS 경로를 공식 제공한다는 의미는 아닙니다. 현재 구현은 DX11 x64용이며 DX12·OpenGL·다른 프로세스·VR 화면은 검증하지 않았습니다. 게임 업데이트로 바뀐 지도는 재추출하며, 그래픽 후킹 호환성도 다시 검사해야 합니다.

렌더링은 게임의 렌더 타깃·깊이 타깃, ImGui 백엔드가 저장하지 않는 hull/domain/compute shader까지 저장·복원합니다. 창 크기 변경 시 backbuffer 참조를 해제합니다. 종료 시 후킹을 비활성화하고 원래 창 메시지 함수를 복원하며, 이미 실행 중인 콜백을 위해 비활성 트램펄린과 DLL 모듈은 프로세스 종료까지 유지합니다. DX11 테스트 호스트에서 표시·resize·타깃 복원·종료를 확인했고 실제 주행 안내의 정확도 검증은 별개입니다.

디자인은 [TMAP 내비게이션](https://www.tmapmobility.com/service/drive/navigation)의 방향 안내 구성을 참고해 HTML/CSS와 자체 SVG 화살표로 만들었습니다. 초록색 방향 영역, 큰 흰색 거리 숫자, 검정 차로 표시줄, 노란 추천 화살표를 적용했습니다. TMAP 이미지나 로고는 앱에 포함하지 않았습니다.
