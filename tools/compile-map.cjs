'use strict';
const fs=require('node:fs'), path=require('node:path');
const {distance,spline,turn,project}=require('../src/geometry.cjs');
const {approachSignals}=require('../src/signal-map.cjs');
const raw=path.join(__dirname,'../data/raw');
const read=name=>JSON.parse(fs.readFileSync(path.join(raw,`europe-${name}.json`),'utf8'));
const nodes=new Map(read('nodes').map(n=>[n.uid,n]));
const looks=new Map(read('roadLooks').map(n=>[n.token,n]));
const descriptions=new Map(read('prefabDescriptions').map(n=>[n.token,n]));
const roads=read('roads'), prefabs=read('prefabs'), cities=read('cities');
const edges=[], roadAt=new Map(), prefabAt=new Map();
const countAt=(map,key)=>map.set(key,(map.get(key)||0)+1);
const round=p=>p.map(v=>Math.round(v*100)/100);
const drivable=l=>!/(train|tram|no_vehicles)/.test(l);
const stats={roads:0,junctions:0,guides:0,signalApproaches:0,unmappedPrefabs:0,excludedRoads:0};
for(const r of roads) {
  const a=nodes.get(r.startNodeUid), b=nodes.get(r.endNodeUid), look=looks.get(r.roadLookToken);
  if(r.hidden||r.secret||!a||!b||!look) {stats.excludedRoads++;continue;}
  const p=spline(a,b,Math.max(4,Math.min(64,Math.ceil(distance([a.x,a.y],[b.x,b.y])/15)))).map(round);
  let added=false;
  for(const [reverse,lanes] of [[false,look.lanesRight],[true,look.lanesLeft]]) {
    if(!lanes.length||!lanes.every(drivable)) continue;
    const from=reverse?b:a, to=reverse?a:b;
    edges.push({kind:'road',uid:r.uid,from:from.uid+':out',to:to.uid+':in',lanes:lanes.length,points:reverse?[...p].reverse():p});
    added=true;
  }
  if(added) {countAt(roadAt,a.uid);countAt(roadAt,b.uid);stats.roads++;}
}
// Cache local prefab topology; every installed instance uses this same template.
const topologyCache=new Map();
function topology(d) {
  if(topologyCache.has(d.token)) return topologyCache.get(d.token);
  const result=[];
  for(let entry=0;entry<d.nodes.length;entry++) {
    const inputs=d.nodes[entry].inputLanes.filter(i=>d.navCurves[i]);
    if(!inputs.length) continue;
    const first=d.navCurves[inputs[0]], forward=[Math.cos(first.start.rotation),Math.sin(first.start.rotation)];
    inputs.sort((a,b)=>{
      const pa=d.navCurves[a].start,pb=d.navCurves[b].start;
      return (pa.x-pb.x)*-forward[1]+(pa.y-pb.y)*forward[0];
    });
    const paths=new Map();
    inputs.forEach((start,lane)=>{
      const queue=[[start]], visited=new Set([start]);
      for(let q=0;q<queue.length;q++) {
        const chain=queue[q], last=chain.at(-1);
        for(let exit=0;exit<d.nodes.length;exit++) {
          if(exit===entry||!d.nodes[exit].outputLanes.includes(last)) continue;
          const curve=d.navCurves[last];
          const direction=turn(forward,[Math.cos(curve.end.rotation),Math.sin(curve.end.rotation)]);
          if(direction==='uturn') continue;
          if(!paths.has(exit)) paths.set(exit,[]);
          paths.get(exit).push({lane,chain,direction});
        }
        for(const next of d.navCurves[last].nextLines) {
          if(!d.navCurves[next]||visited.has(next)) continue;
          visited.add(next); queue.push([...chain,next]);
        }
      }
    });
    const laneDirections=inputs.map((_,lane)=>[...new Set([...paths.values()].flat().filter(p=>p.lane===lane).map(p=>p.direction))]);
    for(const [exit,options] of paths) {
      const recommended=[...new Set(options.map(o=>o.lane))].sort((a,b)=>a-b);
      const representative=options[Math.floor(options.length/2)];
      // A complete lane topology is required; partial templates never invent arrows.
      const guide=inputs.length>1&&inputs.length<=6&&paths.size>1&&laneDirections.every(l=>l.length)
        ?{lanes:laneDirections,recommended,turn:representative.direction}:null;
      const continuationLanes=paths.size===1&&recommended.length===inputs.length&&d.nodes[exit].outputLanes.length===inputs.length?inputs.length:0;
      const navigationTurn=paths.size>1?representative.direction:null;
      result.push({entry,exit,chain:representative.chain,guide,navigationTurn,continuationLanes,signals:approachSignals(d,options,representative)});
    }
  }
  topologyCache.set(d.token,result);return result;
}
for(const prefab of prefabs) {
  if(prefab.hidden||prefab.secret) continue;
  const d=descriptions.get(prefab.token), origin=nodes.get(prefab.nodeUids[0]);
  if(!d||!origin||!d.nodes[prefab.originNodeIndex]) continue;
  const local=d.nodes[prefab.originNodeIndex], angle=origin.rotation-local.rotation;
  const c=Math.cos(angle),s=Math.sin(angle);
  const tx=p=>[origin.x+(p.x-local.x)*c-(p.y-local.y)*s,origin.y+(p.x-local.x)*s+(p.y-local.y)*c,origin.z+(p.z||0)-(local.z||0)];
  // Match physical endpoints by transformed position, independent of prefab node ordering.
  const mapping=d.nodes.map(n=>{
    const p=tx(n);let found=null,best=2;
    for(const id of prefab.nodeUids) {const world=nodes.get(id);if(!world) continue;const dist=distance(p,[world.x,world.y]);if(dist<best){best=dist;found=id;}}
    return found;
  });
  if(mapping.some(n=>!n)||new Set(mapping).size!==mapping.length) {stats.unmappedPrefabs++;continue;}
  mapping.forEach(id=>countAt(prefabAt,id));
  for(const connection of topology(d)) {
    let points=[];
    for(const i of connection.chain) {
      const curve=d.navCurves[i];
      points.push(...spline(curve.start,curve.end,8).map(p=>round(tx({x:p[0],y:p[1],z:p[2]}))));
    }
    if(points.length<2) continue;
    const guide=connection.guide;
    let signalData={};
    if(prefab.showSemaphores&&connection.signals){
      const signalTargets=connection.signals.groups.map(g=>({id:g.id,positions:g.locators.map(l=>{const p=round(tx(l));return {x:p[0],y:p[2],z:p[1]};})}));
      const stop=tx(connection.signals.stop),signalStopAlong=project(stop,points)?.along;
      if(Number.isFinite(signalStopAlong)){signalData={signalTargets,signalStopAlong};stats.signalApproaches++;}
    }
    edges.push({kind:'junction',uid:prefab.uid,from:mapping[connection.entry]+':in',to:mapping[connection.exit]+':out',points,...(guide?{guide}:{}),...(connection.navigationTurn?{navigationTurn:connection.navigationTurn}:{}),...signalData,continuationLanes:connection.continuationLanes,prefab:prefab.token});
    stats.junctions++;if(guide) stats.guides++;
  }
}
for(const [uid,n] of nodes) {
  // Consecutive roads / consecutive prefabs share a boundary without the other type.
  const roads=roadAt.get(uid)||0,prefabs=prefabAt.get(uid)||0;
  if(roads>=2&&prefabs===0) edges.push({kind:'join',uid,from:uid+':in',to:uid+':out',points:[[n.x,n.y,n.z],[n.x+0.001,n.y,n.z]]});
  if(prefabs>=2&&roads===0) edges.push({kind:'join',uid,from:uid+':out',to:uid+':in',points:[[n.x,n.y,n.z],[n.x+0.001,n.y,n.z]]});
}
const result={schema:1,gameVersion:fs.readFileSync(path.join(raw,'europe-version.txt'),'utf8').trim(),generatedAt:new Date().toISOString(),stats,
  cities:cities.map(({token,name,x,y,countryToken})=>({token,name,x,y,country:countryToken})).sort((a,b)=>a.name.localeCompare(b.name)),edges};
const target=path.join(__dirname,'../data/map.json');
fs.writeFileSync(target+'.tmp',JSON.stringify(result));
fs.renameSync(target+'.tmp',target);
console.log(JSON.stringify({...stats,edges:edges.length,cities:cities.length,sizeMB:fs.statSync(path.join(__dirname,'../data/map.json')).size/1e6},null,2));
