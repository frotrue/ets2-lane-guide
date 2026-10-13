#include "ingame_engine.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
static unsigned checks=0;
static void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
static void checkLanePreview(){
 using namespace lane;
 auto map=RoadMap::demonstration();
 const auto junction=map->edges[1],exit=map->edges[2];
 map->edges[0].lanes=2;map->edges[0].points={{0,2000,0},{0,1000,0}};
 Edge join;join.kind=2;join.from=1;join.to=4;join.points={{0,1000,0},{0,1000,0}};
 Edge approach;approach.from=4;approach.to=5;approach.lanes=3;approach.points={{0,1000,0},{0,0,0}};
 Edge first=junction;first.from=5;
 Edge second;second.kind=1;second.from=3;second.to=6;second.turn=2;second.directions={2,1};second.recommended=1;second.points={{280,-80,0},{280,-180,0}};
 map->edges={map->edges[0],join,approach,first,exit,second};map->index();
 const std::vector<int> route={0,1,2,3,4,5};
 Navigator nav(*map);nav.followGameRoute(route,true);
 Truck t;t.z=1800;t.speed=30;t.placed=true;t.paused=false;
 auto g=nav.update(t,20000);
 check(g.status==NavStatus::Navigation&&g.distance==1800&&g.turn==4&&g.directions.empty(),"distant preview preserves main maneuver");
 check(g.nextLanes.distance==1800&&g.nextLanes.directions==first.directions&&g.nextLanes.recommended==6,"preview crosses multiple roads and a lane-count change beyond 500m");
 t.speed=0;g=nav.update(t,20100);check(g.nextLanes.distance==1800&&g.nextLanes.recommended==6,"preview persists when stopped");
 t.z=1200;g=nav.update(t,20200);check(g.nextLanes.distance==1200&&g.status==NavStatus::Navigation,"preview distance decreases without changing target");
 t.z=980;g=nav.update(t,20300);check(g.nextLanes.distance==980&&g.nextLanes.recommended==6,"preview survives entry onto final approach road");
 t.speed=30;t.z=360;g=nav.update(t,20400);check(g.status==NavStatus::Navigation&&g.nextLanes.distance==360,"preview remains visible at approach threshold");
 t.z=359;g=nav.update(t,20500);check(g.status==NavStatus::Guidance&&g.distance==359&&g.recommended==6&&g.nextLanes.directions==g.directions,"preview promotes to matching active guide without a gap");
 t.speed=0;g=nav.update(t,20600);check(g.status==NavStatus::Navigation&&g.nextLanes.recommended==6,"slowing down falls back to preview instead of hiding lanes");
 t.speed=30;t.x=40;t.z=-40;t.heading=.875;g=nav.update(t,20700);
 check(g.turn==2&&g.nextLanes.recommended==1&&g.nextLanes.distance==257&&g.directions.empty(),"while traversing first junction preview advances to next guide");
 t.x=150;t.z=-80;t.heading=.75;g=nav.update(t,20800);check(g.status==NavStatus::Guidance&&g.recommended==1&&g.distance==130,"next guide becomes active on its own approach");
 t.x=280;t.z=-175;t.heading=0;g=nav.update(t,20900);check(g.status==NavStatus::Arrived&&g.nextLanes.directions.empty(),"arrival clears preview");
 nav.followGameRoute(route,true);t.x=0;t.z=1800;t.heading=.5;
 g=nav.update(t,21000);check(g.status==NavStatus::OffRoute&&g.nextLanes.directions.empty(),"opposite heading hides preview");
 t.heading=0;t.y=20;g=nav.update(t,21100);check(g.status==NavStatus::OffRoute&&g.nextLanes.directions.empty(),"wrong elevation hides preview");
 t.y=0;t.x=100;g=nav.update(t,21200);check(g.status==NavStatus::OffRoute&&g.nextLanes.directions.empty(),"off-route truck hides preview");t.x=0;
 nav.followGameRoute({0,1,2},false);g=nav.update(t,21300);check(g.nextLanes.directions.empty(),"partial GPS path never previews beyond known topology");
 nav.followGameRoute(route,false);g=nav.update(t,21400);check(g.nextLanes.recommended==6,"known guide before partial-route end may be previewed");
 nav.clear();g=nav.update(t,21500);check(g.status==NavStatus::NoRoute&&g.nextLanes.directions.empty(),"route cancellation clears preview");
 // Same-length GPS reroutes must replace the upcoming lane layout immediately.
 Edge reroute=first;reroute.turn=1;reroute.recommended=1;map->edges.push_back(reroute);map->index();
 nav.followGameRoute({0,1,2,3},true);g=nav.update(t,21550);check(g.nextLanes.recommended==6,"original route preview before reroute");
 nav.followGameRoute({0,1,2,6},true);g=nav.update(t,21600);check(g.turn==1&&g.nextLanes.recommended==1,"same-length reroute replaces preview without cached old lanes");
 map->edges[2].lanes=2;nav.followGameRoute({0,1,2,3},true);g=nav.update(t,21700);check(g.nextLanes.directions.empty(),"unverified target entry lane count withholds preview");map->edges[2].lanes=3;
 map->edges[2].from=99;nav.followGameRoute(route,true);g=nav.update(t,21800);check(g.nextLanes.directions.empty(),"disconnected path cannot provide a preview");map->edges[2].from=4;
 // An earlier turn without lane data remains the main maneuver. Its successor
 // is a preview only, even if that later lane guide is within the distance window.
 map->edges[0].points={{0,200,0},{0,100,0}};
 map->edges[1].kind=1;map->edges[1].turn=2;map->edges[1].continuation=0;map->edges[1].points={{0,100,0},{0,90,0}};
 map->edges[2].points={{0,90,0},{0,0,0}};map->index();nav.followGameRoute(route,true);t.z=180;
 g=nav.update(t,21900);check(g.status==NavStatus::Navigation&&g.turn==2&&g.distance==80&&g.directions.empty()&&g.nextLanes.distance==180&&g.nextLanes.recommended==6,"next lane preview has independent distance and cannot hide earlier turn");
 map->edges[3].directions.clear();map->edges[3].recommended=0;map->edges[5].directions.clear();map->edges[5].recommended=0;
 nav.followGameRoute(route,true);g=nav.update(t,22000);check(g.nextLanes.directions.empty()&&g.turn==2,"route without valid lane data retains maneuver only");
}
int main(int argc,char**argv){try{
 using namespace lane;
 auto map=RoadMap::demonstration();Truck t;t.x=0;t.z=280;t.heading=0;t.speed=30;t.placed=true;t.paused=false;
 check(map->locate(t)==0,"locate forward road");t.heading=.5;check(map->locate(t)==-1,"reject opposite direction");t.heading=0;t.y=20;check(map->locate(t)==-1,"reject wrong elevation");t.y=0;
 auto path=map->route(0,{3});check(path==std::vector<int>({0,1,2}),"directed path");check(map->route(2,{0}).empty(),"one way path");
 Navigator nav(*map);nav.path=path;auto g=nav.update(t,10000);check(g.status==NavStatus::Guidance&&g.distance==280&&g.recommended==6&&g.turn==4,"lane recommendation");
 t.z=1200;g=nav.update(t,10010);check(g.status==NavStatus::Navigation&&g.turn==4&&g.distance==1200&&g.directions.empty()&&g.nextLanes.recommended==6&&g.nextLanes.distance==1200,"distant maneuver includes upcoming lane preview");t.z=280;
 map->edges[0].lanes=2;g=nav.update(t,10020);check(g.status==NavStatus::Navigation&&g.turn==4&&g.directions.empty(),"lane mismatch keeps verified maneuver but hides lane recommendation");map->edges[0].lanes=3;
 const auto savedDirections=map->edges[1].directions;map->edges[1].directions.clear();map->edges[1].recommended=0;map->edges[0].lanes=1;
 g=nav.update(t,10030);check(g.status==NavStatus::Navigation&&g.distance==280&&g.turn==4&&g.directions.empty(),"single-lane junction still shows navigation");
 map->edges[1].directions=savedDirections;map->edges[1].recommended=6;map->edges[0].lanes=3;
 t.x=150;g=nav.update(t,11000);check(g.status==NavStatus::OffRoute&&g.turn==0&&g.distance<0,"off route hides all navigation arrows");t.x=0;t.z=-10;t.heading=.875;g=nav.update(t,12000);check(g.status!=NavStatus::Guidance&&g.turn==0,"passed junction hides arrows");
 t.z=100;t.heading=0;map->edges[1].signals={{7,{{0,0,0}}}};Navigator signalNav(*map);signalNav.path=path;g=signalNav.update(t,13000);check(g.signals.size()==1&&g.signalDistance==100,"route signal selected");
 t.z=-10;t.heading=.875;g=signalNav.update(t,14000);check(g.signals.empty(),"passed signal hidden");
 nav.clear();check(nav.update(t,15000).status==NavStatus::NoRoute,"clear route");
 t.x=0;t.z=280;t.heading=0;
 Navigator partial(*map);partial.followGameRoute({0},false);g=partial.update(t,16000);check(g.turn==0&&g.distance<0,"partial route does not invent next maneuver");
 Edge later=map->edges[1];later.from=3;later.to=4;later.turn=2;later.points={{280,-80,0},{280,-180,0}};map->edges.push_back(later);map->index();
 Navigator multiple(*map);multiple.followGameRoute({0,1,2,3},true);g=multiple.update(t,17000);check(g.turn==4&&g.distance==280,"nearest maneuver wins over later turn");
 t.x=150;t.z=-80;t.heading=.75;g=multiple.update(t,17100);check(g.turn==2&&g.distance==130,"after passing first turn next maneuver advances");
 checkLanePreview();
 if(argc>1){auto real=RoadMap::load(argv[1]);size_t eligible=0,shown=0,navigationOnly=0,navigationShown=0;
  for(size_t r=0;r<real->edges.size();r++){const auto&road=real->edges[r];if(road.kind!=0||road.lanes<2)continue;
   for(int j:real->adjacency[road.to]){const auto&junction=real->edges[j];if(junction.directions.size()!=road.lanes||!junction.recommended)continue;
    const auto&a=road.points[road.points.size()-2];const auto&b=road.points.back();double dx=b.x-a.x,dz=b.z-a.z;if(std::hypot(dx,dz)<1)continue;
    Truck sample;sample.x=(a.x+b.x)/2;sample.z=(a.z+b.z)/2;sample.y=(a.y+b.y)/2;sample.heading=std::atan2(-dx,-dz)/(2*3.14159265358979323846);sample.speed=25;
    Navigator n(*real);n.path={int(r),j};++eligible;if(n.update(sample,20000).status==NavStatus::Guidance)++shown;
   }
  }check(eligible>8000&&shown==eligible,"actual map guidance coverage");printf("Real map: %zu edges, %zu cities, %zu/%zu approach samples\n",real->edges.size(),real->cities.size(),shown,eligible);
  for(size_t r=0;r<real->edges.size();r++){const auto&road=real->edges[r];if(road.kind!=0)continue;
   for(int j:real->adjacency[road.to]){const auto&junction=real->edges[j];if(junction.kind!=1||!junction.turn||junction.recommended)continue;
    const auto&a=road.points[road.points.size()-2];const auto&b=road.points.back();double dx=b.x-a.x,dz=b.z-a.z;if(std::hypot(dx,dz)<1)continue;
    Truck sample;sample.x=(a.x+b.x)/2;sample.z=(a.z+b.z)/2;sample.y=(a.y+b.y)/2;sample.heading=std::atan2(-dx,-dz)/(2*3.14159265358979323846);sample.speed=25;
    Navigator n(*real);n.followGameRoute({int(r),j},false);++navigationOnly;const auto guide=n.update(sample,21000);if(guide.status==NavStatus::Navigation&&guide.turn==junction.turn&&guide.distance>=0&&guide.directions.empty())++navigationShown;
   }
  }check(navigationOnly>100&&navigationOnly==navigationShown,"actual map navigation without lane guides");printf("Navigation without lanes: %zu/%zu approach samples\n",navigationShown,navigationOnly);
 }
 printf("PASS: navigation %u checks\n",checks);return 0;
 }catch(const std::exception&e){printf("FAIL: %s\n",e.what());return 1;}}
