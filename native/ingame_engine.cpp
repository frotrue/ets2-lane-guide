#include "ingame_engine.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>

namespace lane {
static constexpr double PI=3.14159265358979323846;
static uint64_t cell(int x,int z){return (uint64_t(uint32_t(x))<<32)|uint32_t(z);}
Projection project(double x,double z,const std::vector<Point>& points){
 Projection best;double accumulated=0;
 for(size_t i=1;i<points.size();++i){
  const auto&a=points[i-1];const auto&b=points[i];const double dx=b.x-a.x,dz=b.z-a.z,len=std::hypot(dx,dz);
  if(len<.001)continue;
  const double t=std::clamp(((x-a.x)*dx+(z-a.z)*dz)/(len*len),0.,1.);
  const double d=std::hypot(x-a.x-dx*t,z-a.z-dz*t);
  if(!best.valid||d<best.distance)best={true,d,accumulated+len*t,a.y+(b.y-a.y)*t,dx/len,dz/len};
  accumulated+=len;
 }
 return best;
}
class Reader {
 std::ifstream stream;
public:
 explicit Reader(const std::filesystem::path& path):stream(path,std::ios::binary){if(!stream)throw std::runtime_error("map missing");}
 template<class T>T read(){T v{};if(!stream.read(reinterpret_cast<char*>(&v),sizeof(v)))throw std::runtime_error("map truncated");return v;}
 uint32_t count(uint32_t limit){auto v=read<uint32_t>();if(v>limit)throw std::runtime_error("map count invalid");return v;}
 double number(){double v=read<double>();if(!std::isfinite(v)||std::abs(v)>1e9)throw std::runtime_error("map number invalid");return v;}
 std::string text(){std::string v(count(4096),'\0');if(!stream.read(v.data(),v.size()))throw std::runtime_error("map string truncated");return v;}
 void finish(){if(stream.peek()!=EOF)throw std::runtime_error("map trailing data");}
};
std::shared_ptr<RoadMap> RoadMap::load(const std::filesystem::path& file){
 Reader in(file);std::string magic;for(int i=0;i<8;i++)magic+=in.read<char>();if(magic!="LNGMAP02")throw std::runtime_error("unsupported map; rebuild ingame-map.bin");
 auto map=std::make_shared<RoadMap>();map->version=in.text();
 const auto nc=in.count(20000);map->cities.reserve(nc);
 for(uint32_t i=0;i<nc;i++){City c;c.token=in.text();c.name=in.text();c.country=in.text();c.x=in.number();c.z=in.number();map->cities.push_back(std::move(c));}
 const auto nn=in.count(2000000),ne=in.count(2000000);map->adjacency.resize(nn);map->edges.reserve(ne);
 size_t totalPoints=0;
 for(uint32_t i=0;i<ne;i++){
  Edge e;e.kind=in.read<uint8_t>();e.lanes=in.read<uint8_t>();e.continuation=in.read<uint8_t>();e.turn=in.read<uint8_t>();
  e.from=in.read<uint32_t>();e.to=in.read<uint32_t>();
  e.fromUid=in.read<uint64_t>();e.toUid=in.read<uint64_t>();
  if(e.kind>2||e.from>=nn||e.to>=nn||(e.turn!=0&&e.turn!=1&&e.turn!=2&&e.turn!=4&&e.turn!=8))throw std::runtime_error("invalid map edge");
  const auto np=in.count(100000);totalPoints+=np;if(np<2||totalPoints>10000000)throw std::runtime_error("invalid map points");
  e.points.reserve(np);for(uint32_t k=0;k<np;k++){Point p;p.x=in.number();p.z=in.number();p.y=in.number();e.points.push_back(p);}
  const auto nl=in.read<uint8_t>();if(nl>6)throw std::runtime_error("invalid lane count");
  for(unsigned k=0;k<nl;k++){auto d=in.read<uint8_t>();if(!d||d>15)throw std::runtime_error("invalid directions");e.directions.push_back(d);}
  e.recommended=in.read<uint8_t>();if(e.recommended>=(1u<<nl)&&e.recommended)throw std::runtime_error("invalid recommendation");
  const auto ns=in.count(40);for(uint32_t k=0;k<ns;k++){SignalTarget t;t.id=static_cast<int>(in.count(0x7fffffff));const auto npos=in.count(1024);if(!npos)throw std::runtime_error("empty signal group");for(uint32_t p=0;p<npos;p++){SignalPosition pos;pos.x=in.number();pos.y=in.number();pos.z=in.number();t.positions.push_back(pos);}e.signals.push_back(std::move(t));}
  e.signalStop=in.number();map->edges.push_back(std::move(e));
 }
 in.finish();map->index();return map;
}
void RoadMap::index(){
 grid.clear();anchorEdges.clear();uint32_t nodes=0;for(const auto&e:edges)nodes=std::max(nodes,std::max(e.from,e.to)+1);adjacency.assign(nodes,{});
 for(size_t i=0;i<edges.size();i++){
  auto&e=edges[i];e.length=0;for(size_t j=1;j<e.points.size();j++)e.length+=std::hypot(e.points[j].x-e.points[j-1].x,e.points[j].z-e.points[j-1].z);
  adjacency[e.from].push_back(static_cast<int>(i));if(e.fromUid)anchorEdges[e.fromUid].push_back(static_cast<int>(i));if(e.kind!=0)continue;
  std::set<uint64_t> cells;for(const auto&p:e.points){int x=int(std::floor(p.x/200)),z=int(std::floor(p.z/200));for(int dx=-1;dx<=1;dx++)for(int dz=-1;dz<=1;dz++)cells.insert(cell(x+dx,z+dz));}
  for(auto c:cells)grid[c].push_back(static_cast<int>(i));
 }
}
int RoadMap::locate(const Truck&t)const{
 auto found=grid.find(cell(int(std::floor(t.x/200)),int(std::floor(t.z/200))));if(found==grid.end())return -1;
 const double hx=-std::sin(t.heading*PI*2),hz=-std::cos(t.heading*PI*2);double best=1e100;int result=-1;
 for(auto id:found->second){auto p=project(t.x,t.z,edges[id].points);if(!p.valid||p.distance>35||std::abs(p.y-t.y)>6)continue;
  const double alignment=p.tx*hx+p.tz*hz;if(alignment<.45)continue;const double score=p.distance+(1-alignment)*20+std::abs(p.y-t.y)*2;
  if(score<best){best=score;result=id;}
 }return result;
}
std::vector<int> RoadMap::route(int startEdge,const std::vector<uint32_t>& goals)const{
 if(startEdge<0||size_t(startEdge)>=edges.size())return{};
 const auto start=edges[startEdge].to;std::vector<bool>goal(adjacency.size(),false);for(auto g:goals)if(g<goal.size())goal[g]=true;
 using Item=std::pair<double,uint32_t>;std::priority_queue<Item,std::vector<Item>,std::greater<Item>>q;
 std::vector<double>costs(adjacency.size(),std::numeric_limits<double>::infinity());std::vector<int>prev(adjacency.size(),-1);costs[start]=0;q.push({0,start});
 while(!q.empty()){
  auto [cost,n]=q.top();q.pop();if(cost!=costs[n])continue;
  if(goal[n]){std::vector<int>p;auto at=n;while(at!=start){int id=prev[at];if(id<0)return{};p.push_back(id);at=edges[id].from;}std::reverse(p.begin(),p.end());p.insert(p.begin(),startEdge);return p;}
  for(int id:adjacency[n]){const auto&e=edges[id];double next=cost+std::max(1.,e.length)+(e.kind==1?4:0);if(next<costs[e.to]){costs[e.to]=next;prev[e.to]=id;q.push({next,e.to});}}
 }return{};
}
std::vector<uint32_t> RoadMap::cityGoals(int city)const{
 if(city<0||size_t(city)>=cities.size())return{};const auto&c=cities[city];std::vector<std::pair<double,uint32_t>>near;
 for(const auto&e:edges)if(e.kind==0){const auto&p=e.points.back();double d=std::hypot(p.x-c.x,p.z-c.z);if(d<1500)near.push_back({d,e.to});}
 std::sort(near.begin(),near.end());std::vector<uint32_t>goals;for(size_t i=0;i<std::min<size_t>(6,near.size());i++)goals.push_back(near[i].second);return goals;
}
void Navigator::clear(){path.clear();cursor=0;destination=-1;offRouteSince=0;}
void Navigator::followGameRoute(std::vector<int> edges,bool reachesDestination){clear();path=std::move(edges);terminalIsDestination=reachesDestination;}
bool Navigator::start(const Truck&t,int city){
 if(city<0||size_t(city)>=map.cities.size())return false;
 auto next=map.route(map.locate(t),map.cityGoals(city));if(next.empty())return false;
 path=std::move(next);cursor=0;destination=city;offRouteSince=0;terminalIsDestination=true;return true;
}
Guidance Navigator::update(const Truck&t,uint64_t now){
 Guidance out;if(destination>=0)out.destination=map.cities[destination].name;if(path.empty())return out;
 const double hx=-std::sin(t.heading*PI*2),hz=-std::cos(t.heading*PI*2);Projection best;size_t at=0;double score=1e100;
 for(size_t i=cursor?cursor-1:0;i<std::min(path.size(),cursor+9);i++){auto p=project(t.x,t.z,map.edges[path[i]].points);if(!p.valid||p.distance>30||std::abs(p.y-t.y)>6||p.tx*hx+p.tz*hz<.25)continue;double s=p.distance+(i<cursor?3:0);if(s<score){best=p;score=s;at=i;}}
 if(!best.valid){if(!offRouteSince)offRouteSince=now;if(now-offRouteSince>2500&&now-lastReroute>5000){lastReroute=now;if(start(t,destination)){out.status=NavStatus::Rerouting;return out;}}out.status=NavStatus::OffRoute;return out;}
 offRouteSince=0;cursor=std::max(cursor,at);const auto&current=map.edges[path[at]];
 if(at==path.size()-1&&current.length-best.along<18){out.status=terminalIsDestination?NavStatus::Arrived:NavStatus::OffRoute;return out;}
 // A maneuver does not require a complete multi-lane guide. Show the next
 // known route decision while the stricter lane checks below stay independent.
 out.status=NavStatus::Cruise;
 double maneuverAhead=std::max(0.,current.length-best.along);
 for(size_t i=at+1;i<path.size();i++){
  const auto&e=map.edges[path[i]];
  if(e.kind==1&&e.turn){out.status=NavStatus::Navigation;out.turn=e.turn;out.distance=std::round(maneuverAhead);break;}
  maneuverAhead+=e.length;
 }
 double signalAhead=-best.along;
 for(size_t i=at;i<path.size();i++){const auto&e=map.edges[path[i]];if(!e.signals.empty()){double d=signalAhead+e.signalStop;if(d>=0&&d<=180){out.signals=e.signals;out.signalDistance=std::round(d);}break;}signalAhead+=e.length;if(signalAhead>180||(i>at&&e.kind==1&&!e.continuation))break;}
 double ahead=std::max(0.,current.length-best.along);const int lanes=current.kind==0?current.lanes:current.continuation;
 for(size_t i=at+1;i<path.size();i++){const auto&e=map.edges[path[i]];if(e.directions.size()>1&&e.recommended){
   const double lookahead=std::max(110.,std::min(450.,std::abs(t.speed)*12));
   if(lanes==int(e.directions.size())&&ahead<lookahead){out.status=NavStatus::Guidance;out.directions=e.directions;out.recommended=e.recommended;out.turn=e.turn;out.distance=std::round(ahead);return out;}break;
  }
  if((e.kind==0&&e.lanes!=lanes)||(e.kind==1&&e.continuation!=lanes))break;ahead+=e.length;if(ahead>500)break;
 }
 return out;
}
std::shared_ptr<RoadMap> RoadMap::demonstration(){
 auto m=std::make_shared<RoadMap>();m->version="TEST";m->cities.push_back({"demo","Render test","",280,-80});
 Edge a;a.from=0;a.to=1;a.fromUid=10;a.toUid=20;a.lanes=3;a.points={{0,1500,0},{0,450,0},{0,0,0}};
 Edge b;b.kind=1;b.from=1;b.to=2;b.fromUid=20;b.toUid=30;b.turn=4;b.directions={1,5,4};b.recommended=6;b.points={{0,0,0},{80,-80,0}};
 Edge c;c.from=2;c.to=3;c.fromUid=30;c.toUid=40;c.lanes=2;c.points={{80,-80,0},{280,-80,0}};m->edges={a,b,c};m->index();return m;
}
}
