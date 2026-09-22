const test=require('node:test'),assert=require('node:assert/strict');
const {RoadMap,Navigator,recommendation}=require('../src/navigation.cjs');
const {makeDemo}=require('../src/demo.cjs');
const {Telemetry}=require('../src/telemetry.cjs');
const truck=(overrides={})=>({x:0,y:0,z:150,heading:0,speed:25,...overrides});
test('right-hand exit exposes exactly its connected approach lanes',()=>{
 const {navigator}=makeDemo('right');const state=navigator.update(truck());
 assert.equal(state.status,'guidance');assert.deepEqual(state.recommended,[1,2]);assert.equal(state.distance,150);
 assert.equal(state.message,'오른쪽 2개 차로로');
});
test('wrong-way traffic and a vertically overlapping road get no lane recommendation',()=>{
 for(const input of [truck({heading:.5}),truck({y:40}),truck({x:200})]){
  const state=makeDemo().navigator.update(input);assert.equal(state.status,'off-route');assert.deepEqual(state.recommended,[]);
 }
});
test('a lane-count change between approach and junction suppresses incompatible arrows',()=>{
 const demo=makeDemo();demo.map.edges[0].lanes=2;
 assert.equal(demo.navigator.update(truck()).status,'cruise');
});
test('passing a junction clears its obsolete lane instruction',()=>{
 const {navigator}=makeDemo();navigator.update(truck());
 const result=navigator.update(truck({x:130,z:-80,heading:.75}));
 assert.notEqual(result.status,'guidance');assert.deepEqual(result.recommended,[]);
});
test('route finding respects directed edges and impossible destinations',()=>{
 const map=new RoadMap({edges:[{kind:'road',from:'A',to:'B',points:[[0,100,0],[0,0,0]],lanes:1},{kind:'junction',from:'B',to:'C',points:[[0,0,0],[40,0,0]]},{kind:'road',from:'C',to:'D',points:[[40,0,0],[100,0,0]],lanes:1}]});
 assert.deepEqual(map.route(0,['D']),[0,1,2]);assert.equal(map.route(2,['A']),null);
});
test('disconnected source never falls back to synthetic telemetry',()=>{
 const receiver=new Telemetry();const p={protocol:1,source:'ets2-lane-guide',sequence:1,connected:true,paused:false,placed:true,x:0,y:0,z:100,heading:0,speed:20,scale:19};
 assert.equal(receiver.get(1000),null);assert.equal(receiver.ingest(Buffer.from(JSON.stringify(p)),1000),true);
 assert.equal(receiver.get(1200).z,100);assert.equal(receiver.get(2501),null);
 assert.equal(receiver.ingest(Buffer.from('not json'),3000),false);
 assert.equal(receiver.ingest(Buffer.from(JSON.stringify({...p,x:null})),3000),false);
 receiver.ingest(Buffer.from(JSON.stringify({...p,sequence:4})),3000);
 assert.equal(receiver.ingest(Buffer.from(JSON.stringify({...p,sequence:3})),3100),false);
 assert.equal(receiver.ingest(Buffer.from(JSON.stringify({protocol:1,source:p.source,connected:false})),3150),true);
 assert.equal(receiver.get(3150),null);
});
test('lane labels are unambiguous left-to-right, including disconnected choices',()=>{
 assert.equal(recommendation([0],3),'맨 왼쪽 차로로');assert.equal(recommendation([2],3),'맨 오른쪽 차로로');
 assert.equal(recommendation([0,2],4),'왼쪽부터 1·3번째 차로로');
});
test('heading-matched road location rejects the opposite carriageway direction',()=>{
 const {map}=makeDemo();assert.equal(map.locate(truck()).edge,0);assert.equal(map.locate(truck({heading:.5})),null);
});
test('advance warning spans multiple road segments with an unchanged lane count',()=>{
 const demo=makeDemo();const guide=demo.map.edges[1].guide;
 const map=new RoadMap({edges:[
  {kind:'road',from:'a',to:'b',lanes:3,points:[[0,350,0],[0,220,0]]},
  {kind:'junction',from:'b',to:'c',continuationLanes:3,points:[[0,220,0],[0,200,0]]},
  {kind:'road',from:'c',to:'d',lanes:3,points:[[0,200,0],[0,0,0]]},
  {kind:'junction',from:'d',to:'e',guide,points:[[0,0,0],[100,-100,0]]}
 ]});
 const nav=new Navigator(map);nav.path=[0,1,2,3];nav.destination={name:'test'};
 const result=nav.update(truck({z:280,speed:30}));assert.equal(result.status,'guidance');assert.equal(result.distance,280);
 map.edges[1].continuationLanes=2;assert.equal(nav.update(truck({z:280,speed:30})).status,'cruise');
});
