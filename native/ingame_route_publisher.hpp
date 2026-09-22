#pragma once
#include "ingame_route_protocol.hpp"
#include <windows.h>
#include <cstring>

namespace lane {
// Compiled into the existing ETS2LA DLL. This writes only our own shared-memory
// output, never the game's route, controls, files, or input mapping.
class RoutePublisher {
 HANDLE mapping=nullptr;
 RoutePacket* packet=nullptr;
public:
 explicit RoutePublisher(const wchar_t* name=L"Local\\LaneGuideGPSv1"){
  mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(RoutePacket),name);
  if(!mapping)return;
  if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mapping);mapping=nullptr;return;}
  packet=static_cast<RoutePacket*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(RoutePacket)));
  if(!packet){CloseHandle(mapping);mapping=nullptr;}
 }
 ~RoutePublisher(){if(packet)UnmapViewOfFile(packet);if(mapping)CloseHandle(mapping);}
 RoutePublisher(const RoutePublisher&)=delete;
 RoutePublisher&operator=(const RoutePublisher&)=delete;
 void publish(const RouteRecord* records,std::size_t count,RouteStatus status){
  if(!packet)return;
  if(count>RouteCapacity||(count&&!records)){count=0;status=RouteStatus::Unavailable;}
  InterlockedIncrement(reinterpret_cast<volatile LONG*>(&packet->sequence));
  packet->magic=RouteMagic;packet->producer=RouteProducer;packet->count=static_cast<std::uint32_t>(count);
  packet->publishedAt=GetTickCount64();packet->status=status;packet->reserved=0;
  if(count)std::memcpy(packet->records,records,count*sizeof(RouteRecord));
  InterlockedIncrement(reinterpret_cast<volatile LONG*>(&packet->sequence));
 }
};
}
