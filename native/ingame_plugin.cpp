#include "ingame_runtime.hpp"
#include "ingame_gps.hpp"
#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace lane {
static HMODULE moduleHandle=nullptr;
// Lifetime intentionally matches the pinned DLL, including callbacks during shutdown.
struct Runtime {
 std::mutex mutex;
 View view;
 Truck truck;
 std::thread worker;
 std::atomic<bool> stopping{false};
 bool test=false;
};
static Runtime* runtime=nullptr;
View snapshot(){if(!runtime)return{};std::lock_guard<std::mutex> guard(runtime->mutex);return runtime->view;}
static std::filesystem::path moduleDirectory(){
 wchar_t buffer[32768];DWORD n=GetModuleFileNameW(moduleHandle,buffer,32768);
 if(!n||n==32768)throw std::runtime_error("DLL path unavailable");
 return std::filesystem::path(buffer).parent_path();
}
static std::string executableVersion(){
 wchar_t file[32768];if(!GetModuleFileNameW(nullptr,file,32768))return{};
 DWORD ignored=0;const DWORD size=GetFileVersionInfoSizeW(file,&ignored);if(!size)return{};
 std::vector<unsigned char> bytes(size);if(!GetFileVersionInfoW(file,0,size,bytes.data()))return{};
 VS_FIXEDFILEINFO* info=nullptr;UINT length=0;
 if(!VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&length)||length<sizeof(*info))return{};
 return std::to_string(HIWORD(info->dwFileVersionMS))+"."+std::to_string(LOWORD(info->dwFileVersionMS))+"."+std::to_string(HIWORD(info->dwFileVersionLS))+"."+std::to_string(LOWORD(info->dwFileVersionLS));
}
static void run(){
 auto&r=*runtime;
 try {
  if(!rendererStart(moduleHandle))throw std::runtime_error("DirectX 11 hook initialization failed");
  auto map=r.test?RoadMap::demonstration():RoadMap::load(moduleDirectory()/L"lane_guide"/L"map.bin");
  if(!r.test&&executableVersion()!=map->version)throw std::runtime_error("게임 버전이 달라졌습니다. 지도 데이터를 다시 생성해 주세요.");
  Navigator nav(*map);SignalReader signals;
  std::vector<uint64_t> lastRoute;uint64_t lastMatch=0;
  if(r.test)nav.path={0,1,2};
  {std::lock_guard<std::mutex> guard(r.mutex);r.view.map=map;r.view.ready=true;r.view.message.clear();}
  while(!r.stopping){
   Truck truck;{std::lock_guard<std::mutex> guard(r.mutex);truck=r.truck;}
   const auto now=GetTickCount64();
   const bool active=truck.placed&&!truck.paused&&now-truck.receivedAt<1500;
   std::string message;
   GpsRoute gps;
   if(!r.test){
    if(active)gps=readGameRoute();
    if(gps.status!=RouteStatus::Ready){nav.clear();lastRoute.clear();lastMatch=0;}
    else if(gps.nodes!=lastRoute||now-lastMatch>=1000){
     auto path=matchGameRoute(*map,gps.nodes,truck);nav.followGameRoute(std::move(path.edges),path.complete);lastRoute=gps.nodes;lastMatch=now;
    }
    if(active&&gps.status==RouteStatus::Unavailable)message="게임 내비 연결을 기다리는 중";
    else if(active&&gps.status==RouteStatus::NoRoute)message="게임 지도에서 목적지를 설정하세요";
    else if(active&&nav.path.empty())message="이 구간의 차선 연결을 확인하지 못했어요";
   }
   Guidance guidance=active?nav.update(truck,now):Guidance{};
   auto signal=signals.update(guidance.signals,active,map->version);
   // Explicit SDK render-test host only; never enabled for the game's eut2 ID.
   // The renderer labels both this fixture and the entire HUD as a demonstration.
   if(r.test&&active&&truck.z>=0&&truck.z<=180)signal={true,2,17};
   {std::lock_guard<std::mutex> guard(r.mutex);r.view.truck=truck;r.view.active=active;r.view.guidance=std::move(guidance);r.view.signal=signal;r.view.gpsStatus=gps.status;r.view.gpsMatched=!nav.path.empty();r.view.message=message;}
   for(int i=0;i<10&&!r.stopping;i++)Sleep(10);
  }
 } catch(const std::exception&e){std::lock_guard<std::mutex> guard(r.mutex);r.view.message=e.what();}
}
static SCSAPI_VOID position(const scs_string_t,const scs_u32_t,const scs_value_t*v,const scs_context_t){
 if(!runtime)return;std::lock_guard<std::mutex> guard(runtime->mutex);auto&t=runtime->truck;
 t.placed=v&&v->type==SCS_VALUE_TYPE_dplacement;
 if(t.placed){const auto&p=v->value_dplacement;t.x=p.position.x;t.y=p.position.y;t.z=p.position.z;t.heading=p.orientation.heading;t.placed=std::isfinite(t.x)&&std::isfinite(t.y)&&std::isfinite(t.z)&&std::isfinite(t.heading)&&std::abs(t.x)<1e9&&std::abs(t.z)<1e9;}
}
static SCSAPI_VOID speed(const scs_string_t,const scs_u32_t,const scs_value_t*v,const scs_context_t){
 if(!runtime)return;std::lock_guard<std::mutex> guard(runtime->mutex);
 runtime->truck.speed=v&&v->type==SCS_VALUE_TYPE_float&&std::isfinite(v->value_float.value)?v->value_float.value:0;
}
static SCSAPI_VOID event(const scs_event_t id,const void*,const scs_context_t){
 if(!runtime)return;std::lock_guard<std::mutex> guard(runtime->mutex);
 if(id==SCS_TELEMETRY_EVENT_started)runtime->truck.paused=false;
 if(id==SCS_TELEMETRY_EVENT_paused)runtime->truck.paused=true;
 runtime->truck.receivedAt=GetTickCount64();
}
}
SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version,const scs_telemetry_init_params_t* params){
 using namespace lane;
 if(!params||(version!=SCS_TELEMETRY_VERSION_1_00&&version!=SCS_TELEMETRY_VERSION_1_01))return SCS_RESULT_unsupported;
 const auto*api=static_cast<const scs_telemetry_init_params_v100_t*>(params);
 const bool test=api->common.game_id&&std::strcmp(api->common.game_id,"lane.guide.render-test")==0;
 if(!test&&(!api->common.game_id||std::strcmp(api->common.game_id,"eut2")!=0))return SCS_RESULT_unsupported;
 if(runtime)return SCS_RESULT_generic_error;
 if(!api->register_for_channel||!api->register_for_event)return SCS_RESULT_generic_error;
 runtime=new Runtime;runtime->test=test;runtime->view.test=test;runtime->view.message="도로 지도 불러오는 중";
 // Prevent the engine unloading a callback address while another mod chains our WndProc.
 HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&scs_telemetry_init),&pinned);
 const bool ok=api->register_for_event(SCS_TELEMETRY_EVENT_frame_end,event,nullptr)==SCS_RESULT_ok&&
  api->register_for_event(SCS_TELEMETRY_EVENT_started,event,nullptr)==SCS_RESULT_ok&&
  api->register_for_event(SCS_TELEMETRY_EVENT_paused,event,nullptr)==SCS_RESULT_ok&&
  api->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement,SCS_U32_NIL,SCS_VALUE_TYPE_dplacement,SCS_TELEMETRY_CHANNEL_FLAG_no_value,position,nullptr)==SCS_RESULT_ok&&
  api->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_speed,SCS_U32_NIL,SCS_VALUE_TYPE_float,SCS_TELEMETRY_CHANNEL_FLAG_none,speed,nullptr)==SCS_RESULT_ok;
 if(!ok){if(api->common.log)api->common.log(SCS_LOG_TYPE_error,"[Lane Guide in-game] Telemetry registration failed.");return SCS_RESULT_generic_error;}
 runtime->worker=std::thread(run);
 if(api->common.log)api->common.log(SCS_LOG_TYPE_message,"[Lane Guide in-game] Native DX11 HUD starting. F10 settings / F9 visibility / F8 next position. No external process.");
 return SCS_RESULT_ok;
}
SCSAPI_VOID scs_telemetry_shutdown(){
 if(!lane::runtime)return;lane::runtime->stopping=true;
 if(lane::runtime->worker.joinable())lane::runtime->worker.join();lane::rendererStop();
}
extern "C" __declspec(dllexport) unsigned long long lane_guide_test_frames(){return lane::runtime&&lane::runtime->test?lane::rendererFrames():0;}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)lane::moduleHandle=module;return TRUE;}
