#pragma once
#include "ingame_signals.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lane {
struct Point { double x=0,z=0,y=0; };
struct Truck { double x=0,y=0,z=0,heading=0,speed=0; bool placed=false,paused=true; uint64_t receivedAt=0; };
struct City { std::string token,name,country; double x=0,z=0; };
struct Edge {
 uint8_t kind=0,lanes=0,continuation=0,turn=0,recommended=0;
 uint32_t from=0,to=0;
 uint64_t fromUid=0,toUid=0;
 std::vector<Point> points;
 std::vector<uint8_t> directions;
 std::vector<SignalTarget> signals;
 double length=0,signalStop=0;
};
struct Projection { bool valid=false; double distance=0,along=0,y=0,tx=0,tz=0; };
Projection project(double x,double z,const std::vector<Point>& points);
class RoadMap {
 std::unordered_map<uint64_t,std::vector<int>> grid;
public:
 std::unordered_map<uint64_t,std::vector<int>> anchorEdges;
 std::string version;
 std::vector<City> cities;
 std::vector<Edge> edges;
 std::vector<std::vector<int>> adjacency;
 static std::shared_ptr<RoadMap> load(const std::filesystem::path& file);
 static std::shared_ptr<RoadMap> demonstration();
 void index();
 int locate(const Truck& truck) const;
 std::vector<int> route(int startEdge,const std::vector<uint32_t>& goals) const;
 std::vector<uint32_t> cityGoals(int city) const;
};
enum class NavStatus { NoRoute,OffRoute,Rerouting,Arrived,Guidance,Cruise,Navigation };
struct Guidance {
 NavStatus status=NavStatus::NoRoute;
 std::string destination;
 std::vector<uint8_t> directions;
 uint8_t recommended=0,turn=0;
 double distance=-1,signalDistance=-1;
 std::vector<SignalTarget> signals;
};
class Navigator {
 const RoadMap& map;
 size_t cursor=0;
 int destination=-1;
 uint64_t offRouteSince=0,lastReroute=0;
 bool terminalIsDestination=true;
public:
 std::vector<int> path;
 explicit Navigator(const RoadMap& data):map(data){}
 void clear();
 void followGameRoute(std::vector<int> edges,bool reachesDestination);
 bool start(const Truck& truck,int city);
 Guidance update(const Truck& truck,uint64_t now);
};
}
