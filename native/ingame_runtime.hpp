#pragma once
#include "ingame_engine.hpp"
#include "ingame_route_protocol.hpp"
#include <windows.h>

namespace lane {
struct View {
 Truck truck;
 Guidance guidance;
 SignalResult signal;
 std::shared_ptr<const RoadMap> map;
 std::string message;
 bool ready=false,active=false,test=false;
 RouteStatus gpsStatus=RouteStatus::Unavailable;
 bool gpsMatched=false;
};
View snapshot();
bool rendererStart(HMODULE module);
void rendererStop();
unsigned long long rendererFrames();
}
