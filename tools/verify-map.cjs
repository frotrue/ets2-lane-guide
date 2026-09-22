const fs=require('node:fs'),assert=require('node:assert/strict');
const {RoadMap,Navigator}=require('../src/navigation.cjs');
const map=new RoadMap(JSON.parse(fs.readFileSync('data/map.json','utf8')));
let eligible=0,shown=0,invalid=0;const samples=[];
for(const road of map.edges){
 if(road.kind!=='road'||road.lanes<2)continue;
 const choices=(map.adjacency.get(road.to)||[]).map(i=>map.edges[i]).filter(e=>e.guide&&e.guide.lanes.length===road.lanes);
 for(const junction of choices){
  const guide=junction.guide;
  assert(guide.recommended.every(i=>Number.isInteger(i)&&i>=0&&i<guide.lanes.length));
  if(road.points.length<2)continue;
  const a=road.points.at(-2),b=road.points.at(-1),dx=b[0]-a[0],dy=b[1]-a[1];
  if(Math.hypot(dx,dy)<1)continue;
  const t={x:(a[0]+b[0])/2,z:(a[1]+b[1])/2,y:(a[2]+b[2])/2,heading:((Math.atan2(-dx,-dy)/(2*Math.PI))%1+1)%1,speed:25};
  const nav=new Navigator(map);nav.path=[road.id,junction.id];nav.destination={name:'Map validation'};
  const state=nav.update(t);eligible++;
  if(state.status==='guidance'){shown++;if(samples.length<3)samples.push({prefab:junction.prefab,turn:guide.turn,recommended:guide.recommended,message:state.message});}else invalid++;
 }
}
const report={version:map.data.gameVersion,eligible,shown,invalid,samples};
fs.writeFileSync('output/map-verification.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
assert(shown>100,'Expected guidance to be generated from actual extracted map topology');
assert(invalid/eligible<.03,'Unexpected failure matching approach samples');
