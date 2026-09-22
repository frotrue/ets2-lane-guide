#include "ingame_runtime.hpp"
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "MinHook.h"
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace lane {
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Resize=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
static Present originalPresent=nullptr;
static Resize originalResize=nullptr;
static void* presentAddress=nullptr;
static void* resizeAddress=nullptr;
static HWND gameWindow=nullptr;
static WNDPROC previousProc=nullptr;
static ID3D11Device* device=nullptr;
static ID3D11DeviceContext* deviceContext=nullptr;
static ID3D11RenderTargetView* renderTarget=nullptr;
static IDXGISwapChain* gameChain=nullptr;
static ImGuiContext* context=nullptr;
static ImFont* largeFont=nullptr;
static std::recursive_mutex renderMutex;
static std::atomic<bool> stopping{false};
static std::atomic<unsigned long long> frames{0};
static bool settings=false,visible=true,ownsMinhook=false;
static float uiScale=1;
static std::string iniPath;
static std::wstring layoutPath;
static int positionPreset=1;
static bool layoutSaveFailed=false;
static constexpr const char* presetNames[]={"좌측 상단","상단 중앙","우측 상단","좌측 하단","하단 중앙","우측 하단"};
template<class T>static void release(T*&p){if(p){p->Release();p=nullptr;}}
struct ContextScope{ImGuiContext* previous;ContextScope():previous(ImGui::GetCurrentContext()){ImGui::SetCurrentContext(context);}~ContextScope(){ImGui::SetCurrentContext(previous);}};
static void saveLayout(){
 if(layoutPath.empty()){layoutSaveFailed=true;return;}
 const auto preset=std::to_wstring(positionPreset),scale=std::to_wstring(int(std::lround(uiScale*100)));
 const bool savedPreset=WritePrivateProfileStringW(L"HUD",L"Preset",preset.c_str(),layoutPath.c_str())!=FALSE;
 const bool savedScale=WritePrivateProfileStringW(L"HUD",L"ScalePercent",scale.c_str(),layoutPath.c_str())!=FALSE;
 layoutSaveFailed=!savedPreset||!savedScale;
}
static void selectPreset(int preset){positionPreset=preset;visible=true;saveLayout();}
static void nextScale(){
 constexpr float scales[]={.7f,1.f,1.2f,1.4f,1.6f,1.8f};float next=scales[0];
 for(float scale:scales)if(scale>uiScale+.01f){next=scale;break;}
 uiScale=next;saveLayout();
}

static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){
 {std::lock_guard<std::recursive_mutex> guard(renderMutex);
  if(context&&!stopping){
   if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&!(l&(1LL<<30))){
    if(w==VK_F10){settings=!settings;return 0;}
    if(w==VK_F9){visible=!visible;return 0;}
    if(w==VK_F8){if(GetKeyState(VK_SHIFT)&0x8000)nextScale();else selectPreset((positionPreset+((GetKeyState(VK_CONTROL)&0x8000)?5:1))%6);return 0;}
    if(w==VK_ESCAPE&&settings){settings=false;return 0;}
   }
   if((message==WM_KEYUP||message==WM_SYSKEYUP)&&(w==VK_F8||w==VK_F9||w==VK_F10))return 0;
   // The quick panel never owns the mouse. Do not forward mouse messages to
   // ImGui's handler: it would call SetCapture and compete with ETS2's cursor.
  }
 }
 return previousProc?CallWindowProcW(previousProc,window,message,w,l):DefWindowProcW(window,message,w,l);
}
static std::string utf8(const std::wstring&w){int size=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);std::string out(size,'\0');WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),out.data(),size,nullptr,nullptr);return out;}
static bool initialize(IDXGISwapChain* chain){
 DXGI_SWAP_CHAIN_DESC desc{};if(FAILED(chain->GetDesc(&desc))||!desc.OutputWindow)return false;
 DWORD pid=0;GetWindowThreadProcessId(desc.OutputWindow,&pid);if(pid!=GetCurrentProcessId())return false;
 if(FAILED(chain->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(&device))))return false;
 device->GetImmediateContext(&deviceContext);gameWindow=desc.OutputWindow;gameChain=chain;
 auto*previous=ImGui::GetCurrentContext();context=ImGui::CreateContext();ImGui::SetCurrentContext(context);
 auto&io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange|ImGuiConfigFlags_NoMouse;
 io.LogFilename=nullptr;
 wchar_t local[32768];DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
 if(n&&n<32768){std::error_code ec;auto dir=std::filesystem::path(local)/L"ETS2LaneGuide";std::filesystem::create_directories(dir,ec);if(!ec){
  const bool test=snapshot().test;iniPath=utf8((dir/(test?L"render-test-ui.ini":L"native-ui.ini")).wstring());
  layoutPath=(dir/(test?L"render-test-layout.ini":L"native-layout.ini")).wstring();
 }}
 io.IniFilename=iniPath.empty()?nullptr:iniPath.c_str();
 wchar_t windows[32768];if(GetWindowsDirectoryW(windows,32768)){
  auto path=utf8((std::filesystem::path(windows)/L"Fonts"/L"malgun.ttf").wstring());
  ImFontConfig font;font.OversampleH=2;font.OversampleV=1;
  if(!io.Fonts->AddFontFromFileTTF(path.c_str(),20,&font,io.Fonts->GetGlyphRangesKorean()))io.Fonts->AddFontDefault();
  largeFont=io.Fonts->AddFontFromFileTTF(path.c_str(),96,&font,io.Fonts->GetGlyphRangesDefault());
 }
 ImGui::StyleColorsDark();auto&style=ImGui::GetStyle();style.WindowRounding=14;style.FrameRounding=7;style.Colors[ImGuiCol_CheckMark]=ImVec4(.96f,.96f,0,1);style.Colors[ImGuiCol_Button]=ImVec4(.03f,.39f,.26f,1);
 const bool win=ImGui_ImplWin32_Init(gameWindow);const bool dx=win&&ImGui_ImplDX11_Init(device,deviceContext);
 ImGui::SetCurrentContext(previous);
 if(!dx){if(win)ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(context);context=nullptr;release(deviceContext);release(device);gameChain=nullptr;return false;}
 SetLastError(0);previousProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(windowProc)));
 if(!previousProc&&GetLastError()){ContextScope scope;ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(context);context=nullptr;release(deviceContext);release(device);gameChain=nullptr;return false;}
 RECT client{};GetClientRect(gameWindow,&client);uiScale=client.right>=3000?1.4f:1.f;
 if(!layoutPath.empty()){
  const UINT preset=GetPrivateProfileIntW(L"HUD",L"Preset",1,layoutPath.c_str());positionPreset=preset<6?int(preset):1;
  const UINT scale=GetPrivateProfileIntW(L"HUD",L"ScalePercent",int(uiScale*100),layoutPath.c_str());if(scale>=70&&scale<=180)uiScale=scale/100.f;
 }
 return true;
}
static bool target(IDXGISwapChain* chain){if(renderTarget)return true;ID3D11Texture2D* back=nullptr;if(FAILED(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&back))))return false;const auto hr=device->CreateRenderTargetView(back,nullptr,&renderTarget);back->Release();return SUCCEEDED(hr);}
static void arrow(ImDrawList*d,ImVec2 c,float s,int mask,ImU32 color){
 const float width=5*s;auto line=[&](float ax,float ay,float bx,float by){d->AddLine({c.x+ax*s,c.y+ay*s},{c.x+bx*s,c.y+by*s},color,width);};
 auto head=[&](float x,float y,int direction){ImVec2 a{c.x+x*s,c.y+y*s};if(direction==0)d->AddTriangleFilled({a.x,a.y-9*s},{a.x-8*s,a.y+3*s},{a.x+8*s,a.y+3*s},color);else d->AddTriangleFilled({a.x+direction*8*s,a.y},{a.x-direction*3*s,a.y-8*s},{a.x-direction*3*s,a.y+8*s},color);};
 line(0,22,0,-3);if(mask&1){line(0,-3,0,-21);head(0,-22,0);}if(mask&2){line(0,-3,-8,-13);line(-8,-13,-21,-13);head(-23,-13,-1);}if(mask&4){line(0,-3,8,-13);line(8,-13,21,-13);head(23,-13,1);}if(mask&8){line(0,-3,0,-20);line(0,-20,-17,-20);line(-17,-20,-17,0);d->AddTriangleFilled({c.x-17*s,c.y+8*s},{c.x-24*s,c.y-3*s},{c.x-10*s,c.y-3*s},color);}
}
static const char* status(const View&v){if(!v.ready)return "지도를 준비하고 있어요";if(!v.truck.placed)return "트럭 위치를 기다리는 중";if(!v.active)return "일시정지 · 운전을 시작해 주세요";if(!v.message.empty())return v.message.c_str();switch(v.guidance.status){case NavStatus::NoRoute:return "게임 지도에서 목적지를 설정하세요";case NavStatus::OffRoute:return "경로를 다시 확인하고 있어요";case NavStatus::Rerouting:return "새 경로를 찾았어요";case NavStatus::Arrived:return "목적지 부근에 도착했어요";default:return "게임 내비 경로를 따라 주행하세요";}}
static void drawHud(const View&v){
 const auto display=ImGui::GetIO().DisplaySize;if(!visible||display.x<1||display.y<1)return;
 const bool lanes=v.active&&v.guidance.status==NavStatus::Guidance&&!v.guidance.directions.empty()&&v.guidance.recommended;
 const bool maneuver=v.active&&(v.guidance.status==NavStatus::Navigation||v.guidance.status==NavStatus::Guidance)&&v.guidance.turn&&v.guidance.distance>=0;
 const bool signal=v.active&&v.signal.available;
 const float height=154.f+(lanes?80.f:0.f)+(signal?48.f:0.f);
 const float s=std::min(uiScale,std::min(display.x/424,display.y/height)),w=424*s,h=height*s;
 const float maxX=std::max(0.f,display.x-w),maxY=std::max(0.f,display.y-h),margin=24*s;
 const int column=positionPreset%3;
 const float anchorX=column==0?margin:column==1?maxX/2:maxX-margin;
 const float anchorY=positionPreset<3?margin:maxY-margin;
 // Anchor to the viewport every frame, including resolution and HUD size changes.
 ImGui::SetNextWindowPos({std::clamp(anchorX,0.f,maxX),std::clamp(anchorY,0.f,maxY)},ImGuiCond_Always);
 ImGui::SetNextWindowSize({w,h});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::PushStyleColor(ImGuiCol_WindowBg,{0,0,0,0});
 ImGuiWindowFlags flags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings;
 ImGui::Begin("Lane Guide HUD",nullptr,flags);const auto p=ImGui::GetWindowPos();auto*d=ImGui::GetWindowDrawList();
 auto text=[&](float x,float y,float size,ImU32 c,const char*t){d->AddText(size>30&&largeFont?largeFont:ImGui::GetFont(),size*s,{p.x+x*s,p.y+y*s},c,t);};
 auto wrapped=[&](float x,float y,float width,float size,ImU32 c,const char*t){d->AddText(ImGui::GetFont(),size*s,{p.x+x*s,p.y+y*s},c,t,nullptr,width*s);};
 const ImU32 white=IM_COL32(255,255,255,255),yellow=IM_COL32(244,245,0,255),dim=IM_COL32(113,119,119,255);
 d->AddRectFilled(p,{p.x+w,p.y+h},IM_COL32(25,26,28,250),15*s);
 d->AddRectFilled(p,{p.x+w,p.y+128*s},IM_COL32(9,100,66,255),15*s,ImDrawFlags_RoundCornersTop);
 if(maneuver){
  arrow(d,{p.x+62*s,p.y+66*s},1.65f*s,v.guidance.turn,white);
  char distance[64];if(v.guidance.distance>=1000)std::snprintf(distance,sizeof distance,"%.1f km",v.guidance.distance/1000);else std::snprintf(distance,sizeof distance,"%.0f m",v.guidance.distance);
  // Fit unusually long route distances without colliding with other information.
  auto*font=largeFont?largeFont:ImGui::GetFont();const float measured=font->CalcTextSizeA(53,10000,0,distance).x;
  const float distanceSize=std::min(53.f,282.f/std::max(measured,1.f)*53.f);text(118,24,distanceSize,white,distance);
  const char*direction=v.guidance.turn==2?"왼쪽 방향":v.guidance.turn==4?"오른쪽 방향":v.guidance.turn==8?"유턴":"직진";
  text(121,86,19,white,direction);
 }
 else {text(24,25,26,white,"주행 안내");wrapped(24,70,376,18,white,v.ready&&!v.message.empty()?v.message.c_str():status(v));}
 float row=128;
 if(lanes){const auto count=v.guidance.directions.size();for(size_t i=0;i<count;i++){const float x=424.f*(float(i)+.5f)/float(count);arrow(d,{p.x+x*s,p.y+(row+38)*s},.86f*s,v.guidance.directions[i],v.guidance.recommended&(1u<<i)?yellow:dim);}row+=80;}
 if(signal){
  const int state=v.signal.rawState;const ImU32 color=state==8?IM_COL32(98,240,149,255):state==1?IM_COL32(255,206,83,255):IM_COL32(255,117,120,255);
  d->AddLine({p.x+16*s,p.y+row*s},{p.x+408*s,p.y+row*s},IM_COL32(54,58,57,255),s);
  d->AddCircleFilled({p.x+27*s,p.y+(row+24)*s},6*s,color);
  if(state==4)d->AddCircleFilled({p.x+42*s,p.y+(row+24)*s},5*s,IM_COL32(255,206,83,255));
  const char*label=v.test?"시연 신호 · 전환까지":state==8?"녹색 · 전환까지":state==1?"황색 · 전환까지":state==4?"적색·황색 · 전환까지":"적색 · 전환까지";
  text(58,row+14,17,IM_COL32(210,218,214,255),label);
  char timer[48];std::snprintf(timer,sizeof timer,"%d초",v.signal.remainingSeconds);
  const float timerWidth=ImGui::GetFont()->CalcTextSizeA(27,10000,0,timer).x;text(402-timerWidth,row+9,27,color,timer);row+=48;
 }
 d->AddLine({p.x+16*s,p.y+row*s},{p.x+408*s,p.y+row*s},IM_COL32(54,58,57,255),s);
 std::string footer=v.test?"시연 · 실제 주행 아님":v.gpsMatched&&v.active?"게임 내비 연동   ·   F10 설정":"F10 설정   ·   F9 표시   ·   F8 다음 위치";
 text(20,row+6,13,IM_COL32(167,181,175,255),footer.c_str());
 ImGui::End();ImGui::PopStyleColor();ImGui::PopStyleVar();
}
static void drawSettings(const View&v){
 if(!settings)return;const auto display=ImGui::GetIO().DisplaySize;
 const float panelScale=std::min(std::clamp(display.x/1920.f,1.f,1.6f),std::min((display.x-20)/560,(display.y-20)/440));
 if(panelScale<=0)return;
 ImGui::SetNextWindowSize({560*panelScale,440*panelScale});
 ImGui::SetNextWindowPos({display.x/2,display.y/2},ImGuiCond_Always,{.5f,.5f});
 const auto flags=ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar;
 if(ImGui::Begin("주행 안내 · F10으로 닫기",nullptr,flags)){
  ImGui::SetWindowFontScale(panelScale);
  ImGui::TextUnformatted("마우스는 게임에서 그대로 사용합니다.");
  ImGui::TextUnformatted("위치는 F8, 크기는 Shift+F8로 바꾸세요.");
  ImGui::Separator();
  ImGui::TextUnformatted("위치 프리셋");
  const float buttonWidth=(ImGui::GetContentRegionAvail().x-2*ImGui::GetStyle().ItemSpacing.x)/3;
  for(int i=0;i<6;i++){
   if(i%3)ImGui::SameLine();
   const bool selected=positionPreset==i;
   if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.08f,.57f,.38f,1));
   ImGui::Button(presetNames[i],{buttonWidth,40*panelScale});
   if(selected)ImGui::PopStyleColor();
  }
  ImGui::Text("선택: %s",presetNames[positionPreset]);
  ImGui::Text("크기: %.0f%%   ·   F9: 안내창 %s",uiScale*100,visible?"숨기기":"표시");
  ImGui::TextDisabled("F8 다음 위치 · Ctrl+F8 이전 위치 · Shift+F8 크기");
  if(layoutSaveFailed)ImGui::TextWrapped("설정을 저장하지 못했습니다. 이번 실행에서는 선택한 위치를 사용합니다.");
  ImGui::Separator();
  ImGui::TextUnformatted("게임 내비게이션 연동");
  ImGui::TextWrapped("목적지와 경유지는 게임 지도에서 설정하세요. 화살표·거리와 함께 필요한 차선·신호 정보를 표시합니다.");
  ImGui::TextWrapped("%s",status(v));
  ImGui::TextDisabled("선택한 위치와 크기는 다음 실행에도 유지됩니다.");
 }ImGui::End();
}
static HRESULT STDMETHODCALLTYPE presentHook(IDXGISwapChain* chain,UINT interval,UINT flags){
 if(!stopping&&!(flags&DXGI_PRESENT_TEST)){
  std::lock_guard<std::recursive_mutex> guard(renderMutex);
  if(!stopping&&(!gameChain||gameChain==chain)&&((context!=nullptr)||initialize(chain))&&target(chain)){
   ContextScope scope;ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();
   const auto view=snapshot();ImGui::GetIO().MouseDrawCursor=false;drawHud(view);drawSettings(view);ImGui::Render();
   // ImGui's backend restores pipeline state but not OM render targets.
   ID3D11RenderTargetView* saved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ID3D11DepthStencilView* depth=nullptr;
   deviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,saved,&depth);
   ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;ID3D11ComputeShader* cs=nullptr;
   ID3D11ClassInstance* hc[256]{},*dc[256]{},*cc[256]{};UINT nh=256,nd=256,nc=256;
   deviceContext->HSGetShader(&hs,hc,&nh);deviceContext->DSGetShader(&ds,dc,&nd);deviceContext->CSGetShader(&cs,cc,&nc);
   deviceContext->OMSetRenderTargets(1,&renderTarget,nullptr);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
   deviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,saved,depth);
   deviceContext->HSSetShader(hs,hc,nh);deviceContext->DSSetShader(ds,dc,nd);deviceContext->CSSetShader(cs,cc,nc);
   release(hs);release(ds);release(cs);for(UINT i=0;i<nh;i++)release(hc[i]);for(UINT i=0;i<nd;i++)release(dc[i]);for(UINT i=0;i<nc;i++)release(cc[i]);
   for(auto*&v:saved)release(v);release(depth);++frames;
  }
 }
 return originalPresent(chain,interval,flags);
}
static HRESULT STDMETHODCALLTYPE resizeHook(IDXGISwapChain* chain,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags){
 std::lock_guard<std::recursive_mutex> guard(renderMutex);if(chain==gameChain)release(renderTarget);
 return originalResize(chain,count,width,height,format,flags);
}
bool rendererStart(HMODULE module){
 stopping=false;
 WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=module;cls.lpszClassName=L"LaneGuideDX11Probe";
 if(!RegisterClassW(&cls))return false;
 HWND window=CreateWindowW(cls.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,module,nullptr);
 if(!window){UnregisterClassW(cls.lpszClassName,module);return false;}
 DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=1;desc.BufferDesc.Width=64;desc.BufferDesc.Height=64;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
 IDXGISwapChain* dummy=nullptr;ID3D11Device* dummyDevice=nullptr;ID3D11DeviceContext* dummyContext=nullptr;D3D_FEATURE_LEVEL level;
 HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&dummy,&dummyDevice,&level,&dummyContext);
 if(SUCCEEDED(hr)){auto**vtable=*reinterpret_cast<void***>(dummy);presentAddress=vtable[8];resizeAddress=vtable[13];}
 release(dummyContext);release(dummyDevice);release(dummy);DestroyWindow(window);UnregisterClassW(cls.lpszClassName,module);
 if(FAILED(hr))return false;
 auto mh=MH_Initialize();ownsMinhook=mh==MH_OK;if(mh!=MH_OK&&mh!=MH_ERROR_ALREADY_INITIALIZED)return false;
 if(MH_CreateHook(presentAddress,reinterpret_cast<void*>(presentHook),reinterpret_cast<void**>(&originalPresent))!=MH_OK)return false;
 if(MH_CreateHook(resizeAddress,reinterpret_cast<void*>(resizeHook),reinterpret_cast<void**>(&originalResize))!=MH_OK){MH_RemoveHook(presentAddress);presentAddress=nullptr;return false;}
 if(MH_EnableHook(resizeAddress)!=MH_OK||MH_EnableHook(presentAddress)!=MH_OK){MH_DisableHook(presentAddress);MH_DisableHook(resizeAddress);return false;}
 return true;
}
void rendererStop(){
 stopping=true;
 if(presentAddress)MH_DisableHook(presentAddress);if(resizeAddress)MH_DisableHook(resizeAddress);
 std::lock_guard<std::recursive_mutex> guard(renderMutex);
 if(gameWindow&&previousProc&&reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gameWindow,GWLP_WNDPROC))==windowProc)SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previousProc));
 if(context){auto*previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(context);ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(context);if(previous!=context)ImGui::SetCurrentContext(previous);context=nullptr;}
 release(renderTarget);release(deviceContext);release(device);gameChain=nullptr;
 // Keep disabled trampolines allocated until process exit: an in-flight Present
 // may still be returning through one. The module is pinned for the same reason.
}
unsigned long long rendererFrames(){return frames.load();}
}
