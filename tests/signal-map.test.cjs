'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {approachSignals}=require('../src/signal-map.cjs');
const {RoadMap,Navigator}=require('../src/navigation.cjs');

test('signal groups match explicit IDs and preserve duplicate locator alternatives',()=>{
  const option={chain:[0,1]},description={navCurves:[{}, {semaphoreId:7,start:{x:0,y:5,z:0}}],semaphores:[
    {id:2,type:2,x:0,y:1,z:0},{id:7,type:2,x:2,y:5,z:0},{id:7,type:0,x:3,y:5,z:0}
  ]};
  const result=approachSignals(description,[option],option);
  assert.equal(result.groups[0].id,7);
  assert.deepEqual(result.groups[0].locators.map(l=>l.x),[2,3]);
  assert.deepEqual(result.stop,{x:0,y:5,z:0});
  assert.equal(approachSignals(description,[option,{chain:[0]}],option),null);
  assert.equal(approachSignals({...description,semaphores:[{id:7,type:1,x:2,y:5,z:0}]},[option],option),null);
});

test('route signal targets stop at the control line and never survive losing the route',()=>{
  const signalTargets=[{id:7,positions:[{x:3,y:0,z:5}]}];
  const map=new RoadMap({edges:[
    {kind:'road',from:'a',to:'b',lanes:1,points:[[0,200,0],[0,20,0]]},
    {kind:'junction',from:'b',to:'c',points:[[0,20,0],[0,-20,0]],signalTargets,signalStopAlong:15},
    {kind:'road',from:'c',to:'d',lanes:1,points:[[0,-20,0],[0,-200,0]]}
  ]});
  const nav=new Navigator(map);nav.path=[0,1,2];nav.destination={name:'test'};
  const at=z=>({x:0,y:0,z,heading:0,speed:0});
  assert.equal(nav.update(at(195)).signalTargets,undefined);
  assert.deepEqual(nav.update(at(100)).signalTargets,signalTargets);
  assert.deepEqual(nav.update(at(10)).signalTargets,signalTargets);
  assert.equal(nav.update(at(3)).signalTargets,undefined);
  assert.equal(nav.update({...at(0),x:500}).signalTargets,undefined);
  nav.clear();assert.equal(nav.update(at(100)).signalTargets,undefined);
});
