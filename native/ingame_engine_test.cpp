#include "ingame_engine.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
static unsigned checks=0;
static void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
int main(int argc,char**argv){try{
 using namespace lane;
 auto map=RoadMap::demonstration();Truck t;t.x=0;t.z=280;t.heading=0;t.speed=30;t.placed=true;t.paused=false;
 check(map->locate(t)==0,"locate forward road");t.heading=.5;check(map->locate(t)==-1,"reject opposite direction");t.heading=0;t.y=20;check(map->locate(t)==-1,"reject wrong elevation");t.y=0;
 auto path=map->route(0,{3});check(path==std::vector<int>({0,1,2}),"directed path");check(map->route(2,{0}).empty(),"one way path");
 Navigator nav(*map);nav.path=path;auto g=nav.update(t,10000);check(g.status==NavStatus::Guidance&&g.distance==280&&g.recommended==6&&g.turn==4,"lane recommendation");
 t.z=1200;g=nav.update(t,10010);check(g.status==NavStatus::Navigation&&g.turn==4&&g.distance==1200&&g.directions.empty(),"distant maneuver shown before lane approach");t.z=280;
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
