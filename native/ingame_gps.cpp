#include "ingame_gps.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lane {
GpsRoute decodeGameRoute(const RoutePacket&p,uint64_t now){
 if(!p.sequence||(p.sequence&1)||p.magic!=RouteMagic||p.producer!=RouteProducer||p.count>RouteCapacity||p.reserved||p.publishedAt>now||now-p.publishedAt>750)return{};
 if(p.status==RouteStatus::NoRoute)return p.count?GpsRoute{}:GpsRoute{RouteStatus::NoRoute,{}};
 if(p.status!=RouteStatus::Ready||!p.count)return{};
 GpsRoute out;out.status=RouteStatus::Ready;out.nodes.reserve(p.count);
 for(uint32_t i=0;i<p.count;i++){
  const auto&r=p.records[i];
  if(!r.uid||!std::isfinite(r.distance)||!std::isfinite(r.time)||r.distance<0||r.time<0||r.distance>1e8||r.time>1e8)return{};
  // Repeated adjacent boundary nodes do not represent another road.
  if(out.nodes.empty()||out.nodes.back()!=r.uid)out.nodes.push_back(r.uid);
 }
 return out;
}
GpsRoute readGameRoute(const wchar_t* name){
 const auto handle=OpenFileMappingW(FILE_MAP_READ,FALSE,name);if(!handle)return{};
 const auto*view=static_cast<const RoutePacket*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,sizeof(RoutePacket)));
 if(!view){CloseHandle(handle);return{};}
 RoutePacket first{},second{};
 std::memcpy(&first,view,sizeof first);MemoryBarrier();std::memcpy(&second,view,sizeof second);
 UnmapViewOfFile(view);CloseHandle(handle);
 if(std::memcmp(&first,&second,sizeof first))return{};
 return decodeGameRoute(first,GetTickCount64());
}
GpsPath matchGameRoute(const RoadMap&map,const std::vector<uint64_t>&nodes,const Truck&t){
 if(nodes.size()<2||nodes.size()>RouteCapacity)return{};
 struct Segment{std::vector<int>edges;bool complete=false;};
 std::vector<Segment> segments;Segment segment;
 auto finish=[&](){if(!segment.edges.empty())segments.push_back(std::move(segment));segment={};};
 for(size_t i=1;i<nodes.size();i++){
  const auto found=map.anchorEdges.find(nodes[i-1]);int selected=-1;
  if(found!=map.anchorEdges.end())for(int id:found->second){const auto&e=map.edges[id];if(e.kind==2||e.toUid!=nodes[i])continue;if(selected!=-1){selected=-2;break;}selected=id;}
  if(selected<0){finish();continue;} // Missing/ambiguous topology is never replaced by a new route.
  if(!segment.edges.empty()){
   const auto&previous=map.edges[segment.edges.back()];const auto&next=map.edges[selected];
   if(previous.to!=next.from){
    int join=-1;for(int id:map.adjacency[previous.to]){const auto&e=map.edges[id];if(e.kind==2&&e.to==next.from&&e.fromUid==e.toUid){if(join!=-1){join=-2;break;}join=id;}}
    if(join>=0)segment.edges.push_back(join);else finish();
   }
  }
  segment.edges.push_back(selected);segment.complete=i==nodes.size()-1;
 }
 finish();
 const double hx=-std::sin(t.heading*6.28318530717958647692),hz=-std::cos(t.heading*6.28318530717958647692);
 double best=1e100;GpsPath result;
 for(const auto&part:segments)for(size_t i=0;i<part.edges.size();i++){
  const auto&e=map.edges[part.edges[i]];if(e.kind==2)continue;auto p=project(t.x,t.z,e.points);
  if(!p.valid||p.distance>30||std::abs(p.y-t.y)>6)continue;double alignment=p.tx*hx+p.tz*hz;if(alignment<.25)continue;
  const double score=p.distance+(1-alignment)*15+std::abs(p.y-t.y)*2;
  if(score<best){best=score;result.edges.assign(part.edges.begin()+i,part.edges.end());result.complete=part.complete;}
 }
 return result;
}
}
