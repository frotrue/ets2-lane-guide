#include "ingame_gps.hpp"
#include "ingame_route_publisher.hpp"
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
static int checks=0;
static void check(bool condition,const char*message){++checks;if(!condition)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
 using namespace lane;
 RoutePacket packet{};packet.sequence=2;packet.magic=RouteMagic;packet.producer=RouteProducer;packet.count=2;packet.publishedAt=1000;packet.status=RouteStatus::Ready;packet.records[0]={10,100,10};packet.records[1]={20,0,0};
 check(decodeGameRoute(packet,1100).nodes==std::vector<uint64_t>({10,20}),"valid route packet");
 check(decodeGameRoute(packet,1751).status==RouteStatus::Unavailable,"stale publisher hidden");
 check(decodeGameRoute(packet,999).status==RouteStatus::Unavailable,"future clock hidden");
 packet.sequence=3;check(decodeGameRoute(packet,1100).nodes.empty(),"write in progress rejected");packet.sequence=2;
 packet.producer=0;check(decodeGameRoute(packet,1100).nodes.empty(),"incompatible producer rejected");packet.producer=RouteProducer;
 packet.count=6001;check(decodeGameRoute(packet,1100).nodes.empty(),"oversized route rejected");packet.count=2;
 packet.records[0].distance=std::numeric_limits<float>::quiet_NaN();check(decodeGameRoute(packet,1100).nodes.empty(),"invalid coordinates rejected");packet.records[0].distance=100;
 const auto name=L"Local\\LaneGuideGPS-test-"+std::to_wstring(GetCurrentProcessId());
 {
  RoutePublisher producer(name.c_str());check(readGameRoute(name.c_str()).nodes.empty(),"initial mapping not live");
  producer.publish(packet.records,2,RouteStatus::Ready);check(readGameRoute(name.c_str()).nodes==std::vector<uint64_t>({10,20}),"shared memory round trip");
  packet.records[1].uid=30;producer.publish(packet.records,2,RouteStatus::Ready);check(readGameRoute(name.c_str()).nodes==std::vector<uint64_t>({10,30}),"same length reroute updates");
  producer.publish(nullptr,0,RouteStatus::NoRoute);auto empty=readGameRoute(name.c_str());check(empty.status==RouteStatus::NoRoute&&empty.nodes.empty(),"cancel route clears previous destination");
  producer.publish(nullptr,6001,RouteStatus::Ready);check(readGameRoute(name.c_str()).status==RouteStatus::Unavailable,"publisher bound enforced");
 }
 check(readGameRoute(name.c_str()).status==RouteStatus::Unavailable,"producer disconnect clears route");
 auto map=RoadMap::demonstration();Truck t;t.z=280;t.speed=30;
 auto path=matchGameRoute(*map,{10,20,30,40},t);check(path.edges==std::vector<int>({0,1,2})&&path.complete,"exact GPS topology followed");
 Navigator nav(*map);nav.followGameRoute(path.edges,path.complete);check(nav.update(t,10000).turn==4,"GPS exit determines lane arrow");
 Edge straight=map->edges[1];straight.to=4;straight.toUid=50;straight.turn=1;straight.recommended=1;straight.points={{0,0,0},{0,-80,0}};
 Edge onward=map->edges[2];onward.from=4;onward.to=5;onward.fromUid=50;onward.toUid=60;onward.points={{0,-80,0},{0,-280,0}};
 map->edges.push_back(straight);map->edges.push_back(onward);map->index();
 auto rerouted=matchGameRoute(*map,{10,20,50,60},t);nav.followGameRoute(rerouted.edges,rerouted.complete);auto newGuide=nav.update(t,10100);
 check(rerouted.edges==std::vector<int>({0,3,4})&&newGuide.turn==1&&newGuide.recommended==1,"same-length game reroute changes exit and recommended lanes");
 map->edges.resize(3);map->index();
 t.heading=.5;check(matchGameRoute(*map,{10,20,30,40},t).edges.empty(),"GPS wrong way rejected");t.heading=0;
 t.y=20;check(matchGameRoute(*map,{10,20,30,40},t).edges.empty(),"GPS elevation rejected");t.y=0;
 auto partial=matchGameRoute(*map,{10,20,999,40},t);check(partial.edges==std::vector<int>({0})&&!partial.complete,"unknown road is not bridged by a guessed route");
 nav.followGameRoute(partial.edges,partial.complete);t.z=5;check(nav.update(t,11000).status!=NavStatus::Arrived,"map gap never means destination reached");t.z=280;
 map->edges.push_back(map->edges[1]);map->index();check(matchGameRoute(*map,{10,20,30,40},t).edges==std::vector<int>({0}),"ambiguous exit withholds guidance");map->edges.pop_back();
 map->edges[1].from=4;Edge join;join.kind=2;join.from=1;join.to=4;join.fromUid=join.toUid=20;join.points={{0,0,0},{.001,0,0}};map->edges.push_back(join);map->index();
 check(matchGameRoute(*map,{10,20,30,40},t).edges==std::vector<int>({0,3,1,2}),"only explicit boundary joins connect adjacent roads");
 if(argc==3){auto real=RoadMap::load(argv[1]);std::ifstream in(argv[2]);Truck live;in>>live.x>>live.y>>live.z>>live.heading;live.speed=25;std::vector<uint64_t>nodes;std::string uid;while(in>>uid)nodes.push_back(std::stoull(uid,nullptr,16));
  // This captured truck is 57m off the first GPS road and faces the opposite
  // direction. It is a negative fixture, not an on-route driving sample.
  check(matchGameRoute(*real,nodes,live).edges.empty(),"captured off-route truck must not get false guidance");
  int sampleEdge=-1;for(size_t i=1;i<nodes.size()&&sampleEdge<0;i++){auto found=real->anchorEdges.find(nodes[i-1]);if(found==real->anchorEdges.end())continue;for(int id:found->second)if(real->edges[id].kind==0&&real->edges[id].toUid==nodes[i]){sampleEdge=id;break;}}
  check(sampleEdge>=0,"captured GPS node pair exists in map");const auto&points=real->edges[sampleEdge].points;const auto&a=points[points.size()-2];const auto&b=points.back();
  Truck replay;replay.x=(a.x+b.x)/2;replay.y=(a.y+b.y)/2;replay.z=(a.z+b.z)/2;replay.heading=std::atan2(a.x-b.x,a.z-b.z)/6.28318530717958647692;
  auto actual=matchGameRoute(*real,nodes,replay);check(actual.edges.size()>3,"captured game route matches from a synthetic on-road replay position");
  for(size_t i=1;i<actual.edges.size();i++)check(real->edges[actual.edges[i-1]].to==real->edges[actual.edges[i]].from,"captured route remains connected");
  printf("Captured game GPS replay: %zu nodes, matched %zu connected edges ahead, complete=%s; real off-route truck withheld\n",nodes.size(),actual.edges.size(),actual.complete?"yes":"no");
 }
 printf("PASS: GPS %d checks\n",checks);return 0;
 }catch(const std::exception&e){printf("FAIL: %s\n",e.what());return 1;}}
