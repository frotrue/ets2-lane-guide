#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "scssdk_telemetry.h"
static scs_telemetry_event_callback_t on_frame=nullptr,on_pause=nullptr,on_start=nullptr;
static scs_telemetry_channel_callback_t on_pos=nullptr,on_speed=nullptr;
static void* speed_ctx=nullptr;
static SCSAPI_VOID log_line(const scs_log_type_t, const scs_string_t message){puts(message);}
static SCSAPI_RESULT register_event(const scs_event_t id,const scs_telemetry_event_callback_t callback,const scs_context_t){
 if(id==SCS_TELEMETRY_EVENT_frame_end)on_frame=callback;
 if(id==SCS_TELEMETRY_EVENT_started)on_start=callback;
 if(id==SCS_TELEMETRY_EVENT_paused)on_pause=callback;
 return SCS_RESULT_ok;
}
static SCSAPI_RESULT register_channel(const scs_string_t name,const scs_u32_t,const scs_value_type_t,const scs_u32_t,const scs_telemetry_channel_callback_t callback,const scs_context_t context){
 if(strcmp(name,"truck.world.placement")==0)on_pos=callback;
 if(strcmp(name,"truck.speed")==0){on_speed=callback;speed_ctx=context;}
 return SCS_RESULT_ok;
}
int main(int argc,char**argv){
 if(argc!=2)return 10;
 HMODULE dll=LoadLibraryA(argv[1]);if(!dll)return 11;
 using Init=scs_result_t(__stdcall*)(scs_u32_t,const scs_telemetry_init_params_t*);
 using Stop=void(__stdcall*)();
 auto init=reinterpret_cast<Init>(GetProcAddress(dll,"scs_telemetry_init"));
 auto stop=reinterpret_cast<Stop>(GetProcAddress(dll,"scs_telemetry_shutdown"));
 if(!init||!stop)return 12;
 scs_telemetry_init_params_v100_t params{};params.common.log=log_line;
 params.register_for_event=register_event;params.register_for_channel=register_channel;
 if(init(SCS_TELEMETRY_VERSION_1_01,&params)!=SCS_RESULT_ok)return 13;
 if(!on_pos||!on_speed||!on_start||!on_pause||!on_frame)return 14;
 scs_value_t position{};position.type=SCS_VALUE_TYPE_dplacement;
 position.value_dplacement.position.x=1234.5;position.value_dplacement.position.y=10;position.value_dplacement.position.z=-9876.5;
 position.value_dplacement.orientation.heading=.75f;
 on_pos("truck.world.placement",SCS_U32_NIL,&position,nullptr);
 scs_value_t speed{};speed.type=SCS_VALUE_TYPE_float;speed.value_float.value=25;
 on_speed("truck.speed",SCS_U32_NIL,&speed,speed_ctx);
 on_start(SCS_TELEMETRY_EVENT_started,nullptr,nullptr);Sleep(130);on_frame(SCS_TELEMETRY_EVENT_frame_end,nullptr,nullptr);
 on_pause(SCS_TELEMETRY_EVENT_paused,nullptr,nullptr);stop();FreeLibrary(dll);puts("PASS: SDK lifecycle and callbacks");return 0;
}
