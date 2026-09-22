'use strict';
const { distance, length, project } = require('./geometry.cjs');

class MinHeap {
  constructor() { this.a = []; }
  push(value) { const a=this.a; a.push(value); let i=a.length-1; while(i>0) {const p=(i-1)>>1; if(a[p][0]<=value[0]) break; a[i]=a[p]; i=p;} a[i]=value; }
  pop() { const a=this.a, result=a[0], last=a.pop(); if(a.length) {let i=0; while(i*2+1<a.length) {let c=i*2+1; if(c+1<a.length&&a[c+1][0]<a[c][0]) c++; if(a[c][0]>=last[0]) break; a[i]=a[c]; i=c;} a[i]=last;} return result; }
}
class RoadMap {
  constructor(data) {
    this.data=data; this.edges=data.edges; this.adjacency=new Map(); this.grid=new Map();
    for(let i=0;i<this.edges.length;i++) {
      const e=this.edges[i]; e.id=i; e.length=length(e.points);
      if(!this.adjacency.has(e.from)) this.adjacency.set(e.from,[]);
      this.adjacency.get(e.from).push(i);
      if(e.kind!=='road') continue;
      const cells=new Set();
      for(const p of e.points) { const x=Math.floor(p[0]/200), y=Math.floor(p[1]/200); for(let dx=-1;dx<=1;dx++) for(let dy=-1;dy<=1;dy++) cells.add(`${x+dx},${y+dy}`); }
      for(const key of cells) { if(!this.grid.has(key)) this.grid.set(key,[]); this.grid.get(key).push(i); }
    }
  }
  locate(t, maxDistance=35) {
    const candidates=this.grid.get(`${Math.floor(t.x/200)},${Math.floor(t.z/200)}`)||[];
    const heading=[-Math.sin(t.heading*Math.PI*2),-Math.cos(t.heading*Math.PI*2)];
    let best=null;
    for(const id of candidates) {
      const e=this.edges[id], p=project([t.x,t.z],e.points);
      if(!p||p.distance>maxDistance||Math.abs(p.point[2]-t.y)>6) continue;
      const alignment=p.tangent[0]*heading[0]+p.tangent[1]*heading[1];
      if(alignment<0.45) continue;
      const score=p.distance+(1-alignment)*20+Math.abs(p.point[2]-t.y)*2;
      if(!best||score<best.score) best={edge:id,...p,score};
    }
    return best;
  }
  route(startEdge, goalNodes) {
    const start=this.edges[startEdge].to, goals=new Set(goalNodes), queue=new MinHeap();
    const costs=new Map([[start,0]]), prev=new Map(); queue.push([0,start]);
    while(queue.a.length) {
      const [cost,node]=queue.pop(); if(cost!==costs.get(node)) continue;
      if(goals.has(node)) {
        const path=[]; let at=node;
        while(at!==start) { const edge=prev.get(at); if(edge===undefined) return null; path.push(edge); at=this.edges[edge].from; }
        path.reverse(); return [startEdge,...path];
      }
      for(const id of this.adjacency.get(node)||[]) {
        const e=this.edges[id];
        const candidate=cost+Math.max(1,e.length)+(e.kind==='junction'?4:0);
        if(candidate<(costs.get(e.to)??Infinity)) {costs.set(e.to,candidate); prev.set(e.to,id); queue.push([candidate,e.to]);}
      }
    }
    return null;
  }
  cityGoals(city) {
    return this.edges.filter(e=>e.kind==='road').map(e=>({node:e.to,d:distance(e.points.at(-1),[city.x,city.y])}))
      .filter(e=>e.d<1500).sort((a,b)=>a.d-b.d).slice(0,6).map(e=>e.node);
  }
}
class Navigator {
  constructor(map) { this.map=map; this.path=[]; this.index=0; this.destination=null; this.offRouteSince=0; this.lastReroute=0; }
  clear() { this.path=[]; this.index=0; this.destination=null; this.offRouteSince=0; }
  start(t,city) {
    const location=this.map.locate(t);
    if(!location) throw new Error('현재 도로를 찾지 못했습니다. 일반 도로로 이동한 뒤 다시 시작하세요.');
    const route=this.map.route(location.edge,this.map.cityGoals(city));
    if(!route) throw new Error('연결된 육로 경로를 찾지 못했습니다. 가까운 도시를 선택해 주세요.');
    this.path=route; this.index=0; this.destination=city; this.offRouteSince=0;
    return route;
  }
  update(t,now=Date.now()) {
    const base={destination:this.destination?.name||'',lanes:[],recommended:[],distance:null};
    if(!this.path.length) return {...base,status:'no-route',message:'목적지를 선택하세요'};
    const heading=[-Math.sin(t.heading*Math.PI*2),-Math.cos(t.heading*Math.PI*2)];
    let best=null;
    // Small forward window prevents jumping to a later section on a looping route.
    for(let i=Math.max(0,this.index-1);i<Math.min(this.path.length,this.index+9);i++) {
      const edge=this.map.edges[this.path[i]], p=project([t.x,t.z],edge.points);
      if(!p||p.distance>30||Math.abs(p.point[2]-t.y)>6||p.tangent[0]*heading[0]+p.tangent[1]*heading[1]<0.25) continue;
      const score=p.distance+(i<this.index?3:0);
      if(!best||score<best.score) best={...p,index:i,score};
    }
    if(!best) {
      this.offRouteSince ||= now;
      if(now-this.offRouteSince>2500&&now-this.lastReroute>5000) {
        this.lastReroute=now;
        try {this.start(t,this.destination); return {...base,status:'rerouting',message:'경로를 다시 찾았습니다'};} catch { /* retain the target, withhold guidance */ }
      }
      return {...base,status:'off-route',message:'현재 도로 확인 중',detail:'위치가 확인되면 안내를 재개합니다'};
    }
    this.offRouteSince=0; this.index=Math.max(this.index,best.index);
    const current=this.map.edges[this.path[best.index]];
    if(best.index===this.path.length-1&&current.length-best.along<18) return {...base,status:'arrived',message:'목적지 도시 근처입니다'};
    // Route-linked signals are useful even at a single-lane intersection.
    let signalAhead=-best.along;
    for(let i=best.index;i<this.path.length;i++) {
      const edge=this.map.edges[this.path[i]];
      if(edge.signalTargets?.length) {
        const stopDistance=signalAhead+edge.signalStopAlong;
        if(Number.isFinite(stopDistance)&&stopDistance>=0&&stopDistance<=180) {
          base.signalTargets=edge.signalTargets;
          base.signalDistance=Math.round(stopDistance);
        }
        break;
      }
      signalAhead+=edge.length;
      if(signalAhead>180) break;
      if(i>best.index&&edge.kind==='junction'&&!edge.continuationLanes) break;
    }
    let ahead=Math.max(0,current.length-best.along);
    const approachLanes=current.kind==='road'?current.lanes:current.continuationLanes;
    for(let i=best.index+1;i<this.path.length;i++) {
      const edge=this.map.edges[this.path[i]];
      if(edge.guide&&edge.guide.recommended.length&&edge.guide.lanes.length>1) {
        // Carry guidance across segments only when every intervening segment
        // preserves the approach lane count and has no other exit choice.
        const lookahead=Math.max(110,Math.min(450,Math.abs(t.speed)*12));
        if(approachLanes===edge.guide.lanes.length&&ahead<lookahead) {
          return {...base,...edge.guide,status:'guidance',distance:Math.round(ahead),message:recommendation(edge.guide.recommended,edge.guide.lanes.length),detail:edge.guide.turn==='right'?'오른쪽 방향':edge.guide.turn==='left'?'왼쪽 방향':'직진 방향'};
        }
        break;
      }
      if(edge.kind==='road'&&edge.lanes!==approachLanes) break;
      if(edge.kind==='junction'&&edge.continuationLanes!==approachLanes) break;
      ahead+=edge.length;
      if(ahead>500) break;
    }
    return {...base,status:'cruise',message:'경로를 따라 주행하세요',detail:'분기점에 가까워지면 차선을 안내합니다'};
  }
}
function recommendation(indices,count) {
  if(indices.length===count) return '현재 차로를 유지하세요';
  if(indices.length===1&&indices[0]===count-1) return '맨 오른쪽 차로로';
  if(indices.length===1&&indices[0]===0) return '맨 왼쪽 차로로';
  if(indices.every((v,i)=>v===count-indices.length+i)) return `오른쪽 ${indices.length}개 차로로`;
  if(indices.every((v,i)=>v===i)) return `왼쪽 ${indices.length}개 차로로`;
  return `왼쪽부터 ${indices.map(i=>i+1).join('·')}번째 차로로`;
}
module.exports={RoadMap,Navigator,recommendation};
