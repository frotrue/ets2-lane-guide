const fs=require('node:fs');
const {RoadMap}=require('../src/navigation.cjs');
const data=JSON.parse(fs.readFileSync('data/map.json','utf8'));
const map=new RoadMap(data);
const results=[];
for(const [a,b] of [['berlin','hamburg'],['paris','lyon'],['london','dover']]) {
 const start=data.cities.find(c=>c.token===a), goal=data.cities.find(c=>c.token===b);
 if(!start||!goal) continue;
 const near=map.edges.filter(e=>e.kind==='road').map(e=>({e,d:Math.hypot(e.points[0][0]-start.x,e.points[0][1]-start.y)})).sort((a,b)=>a.d-b.d);
 let route=null;for(const x of near.slice(0,6)){route=map.route(x.e.id,map.cityGoals(goal));if(route) break;}
 results.push({from:a,to:b,found:!!route,edges:route?.length,guides:route?.filter(i=>map.edges[i].guide).length});
}
console.log(JSON.stringify(results,null,2));
if(results.some(r=>!r.found)) process.exitCode=1;
