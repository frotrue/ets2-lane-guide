#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstring>
#include "scssdk_telemetry.h"
static scs_telemetry_event_callback_t frame=nullptr,started=nullptr,paused=nullptr;
static scs_telemetry_channel_callback_t position=nullptr,speed=nullptr;
static IDXGISwapChain* chain=nullptr;
static ID3D11Device* device=nullptr;
static ID3D11DeviceContext* ctx=nullptr;
static ID3D11RenderTargetView* rtv=nullptr;
static bool done=false;
template<class T>static void release(T*&p){if(p){p->Release();p=nullptr;}}
static SCSAPI_VOID logLine(const scs_log_type_t,const scs_string_t message){puts(message);}
static SCSAPI_RESULT registerEvent(const scs_event_t id,const scs_telemetry_event_callback_t cb,const scs_context_t){if(id==SCS_TELEMETRY_EVENT_frame_end)frame=cb;if(id==SCS_TELEMETRY_EVENT_started)started=cb;if(id==SCS_TELEMETRY_EVENT_paused)paused=cb;return SCS_RESULT_ok;}
static SCSAPI_RESULT registerChannel(const scs_string_t name,const scs_u32_t,const scs_value_type_t,const scs_u32_t,const scs_telemetry_channel_callback_t cb,const scs_context_t){if(!strcmp(name,"truck.world.placement"))position=cb;if(!strcmp(name,"truck.speed"))speed=cb;return SCS_RESULT_ok;}
static LRESULT CALLBACK wndProc(HWND window,UINT msg,WPARAM w,LPARAM l){
 if(msg==WM_DESTROY){done=true;PostQuitMessage(0);return 0;}
 if(msg==WM_SIZE&&chain&&w!=SIZE_MINIMIZED){ctx->OMSetRenderTargets(0,nullptr,nullptr);release(rtv);auto hr=chain->ResizeBuffers(0,LOWORD(l),HIWORD(l),DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr)){printf("FAIL ResizeBuffers %08lx\n",hr);done=true;}return 0;}
 return DefWindowProcW(window,msg,w,l);
}
int wmain(int argc,wchar_t**argv){
 if(argc<2){puts("usage: ingame_render_host plugin.dll [--self-test] [--far|--signal-demo]");return 10;}
 bool automated=false;double demoDistance=280;
 for(int i=2;i<argc;i++){if(!wcscmp(argv[i],L"--self-test"))automated=true;else if(!wcscmp(argv[i],L"--far"))demoDistance=1200;else if(!wcscmp(argv[i],L"--signal-demo"))demoDistance=100;else return 10;}
 auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.lpfnWndProc=wndProc;cls.hInstance=instance;cls.lpszClassName=L"LaneGuideRenderTestHost";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&cls);
 HWND window=CreateWindowW(cls.lpszClassName,L"Lane Guide · Native DX11 test (not ETS2)",WS_OVERLAPPEDWINDOW,80,80,1280,850,nullptr,nullptr,instance,nullptr);
 DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=1;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
 D3D_FEATURE_LEVEL level;auto hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,&level,&ctx);if(FAILED(hr))return 11;
 auto dll=LoadLibraryW(argv[1]);if(!dll){printf("load error %lu\n",GetLastError());return 12;}
 using Init=scs_result_t(__stdcall*)(scs_u32_t,const scs_telemetry_init_params_t*);using Stop=void(__stdcall*)();using Count=unsigned long long(*)();
 auto init=reinterpret_cast<Init>(GetProcAddress(dll,"scs_telemetry_init"));auto stop=reinterpret_cast<Stop>(GetProcAddress(dll,"scs_telemetry_shutdown"));auto count=reinterpret_cast<Count>(GetProcAddress(dll,"lane_guide_test_frames"));if(!init||!stop||!count)return 13;
 scs_telemetry_init_params_v100_t api{};api.common.game_id="lane.guide.render-test";api.common.game_name="Native DX11 render test";api.common.log=logLine;api.register_for_channel=registerChannel;api.register_for_event=registerEvent;
 if(init(SCS_TELEMETRY_VERSION_1_01,&api)!=SCS_RESULT_ok)return 14;
 if(!frame||!started||!paused||!position||!speed)return 15;
 scs_value_t p{};p.type=SCS_VALUE_TYPE_dplacement;p.value_dplacement.position.z=demoDistance;position("truck.world.placement",SCS_U32_NIL,&p,nullptr);
 scs_value_t v{};v.type=SCS_VALUE_TYPE_float;v.value_float.value=30;speed("truck.speed",SCS_U32_NIL,&v,nullptr);started(SCS_TELEMETRY_EVENT_started,nullptr,nullptr);
 ShowWindow(window,SW_SHOW);const auto begin=GetTickCount64();bool resized=false;unsigned presented=0;int result=0;
 while(!done){
  MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(done)break;
  frame(SCS_TELEMETRY_EVENT_frame_end,nullptr,nullptr);
  if(!rtv){ID3D11Texture2D* back=nullptr;chain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&back));if(!back||FAILED(device->CreateRenderTargetView(back,nullptr,&rtv))){release(back);result=16;break;}release(back);}
  const float clear[]={.035f,.065f,.09f,1};ctx->OMSetRenderTargets(1,&rtv,nullptr);ctx->ClearRenderTargetView(rtv,clear);
  hr=chain->Present(1,0);if(FAILED(hr)){result=17;break;}++presented;
  ID3D11RenderTargetView* after=nullptr;ctx->OMGetRenderTargets(1,&after,nullptr);bool retained=after==rtv;release(after);if(!retained){puts("FAIL game render target was not restored");result=18;break;}
  if(automated&&count()>60&&!resized){SetWindowPos(window,nullptr,0,0,1100,760,SWP_NOMOVE|SWP_NOZORDER);resized=true;}
  if(automated&&resized&&count()>150)break;
  if(automated&&GetTickCount64()-begin>15000){printf("TIMEOUT frames=%llu\n",count());result=19;break;}
 }
 const auto rendered=count();paused(SCS_TELEMETRY_EVENT_paused,nullptr,nullptr);stop();
 if(automated&&!result){const float black[]={0,0,0,1};for(int i=0;i<3;i++){ctx->ClearRenderTargetView(rtv,black);if(FAILED(chain->Present(0,0)))result=20;}if(count()!=rendered)result=21;}
 ctx->OMSetRenderTargets(0,nullptr,nullptr);release(rtv);release(chain);release(ctx);release(device);DestroyWindow(window);FreeLibrary(dll);
 printf("%s: native Present %llu / %u, resize=%s, render-target restore, SDK shutdown\n",result?"FAIL":"PASS",rendered,presented,resized?"yes":"no");return result;
}
