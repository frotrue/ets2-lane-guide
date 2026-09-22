#pragma once
#include "ingame_engine.hpp"
#include "ingame_route_protocol.hpp"
namespace lane {
struct GpsRoute { RouteStatus status=RouteStatus::Unavailable;std::vector<uint64_t> nodes; };
struct GpsPath { std::vector<int> edges;bool complete=false; };
GpsRoute decodeGameRoute(const RoutePacket& packet,uint64_t now);
GpsRoute readGameRoute(const wchar_t* name=L"Local\\LaneGuideGPSv1");
GpsPath matchGameRoute(const RoadMap& map,const std::vector<uint64_t>& nodes,const Truck& truck);
}
