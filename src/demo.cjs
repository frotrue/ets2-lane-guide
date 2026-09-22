'use strict';
const { RoadMap,Navigator }=require('./navigation.cjs');
function makeDemo(scenario='right') {
  const isLeft=scenario==='left', isStraight=scenario==='straight';
  const guide={lanes:isLeft?[['left'],['straight','left'],['straight']]:[['straight'],['straight','right'],['right']],recommended:isLeft?[0,1]:isStraight?[0,1]:[1,2],turn:isLeft?'left':isStraight?'straight':'right'};
  const data={cities:[],edges:[
    {kind:'road',from:'a:out',to:'b:in',lanes:3,points:[[0,450,0],[0,0,0]]},
    {kind:'junction',from:'b:in',to:'c:out',points:[[0,0,0],[isLeft?-80:80,-80,0]],guide},
    {kind:'road',from:'c:out',to:'d:in',lanes:2,points:[[isLeft?-80:80,-80,0],[isLeft?-280:280,-80,0]]}
  ]};
  const map=new RoadMap(data), navigator=new Navigator(map);
  navigator.path=[0,1,2];navigator.destination={name:'시연 경로'};
  return {map,navigator,startedAt:Date.now(),scenario};
}
function demoState(demo,now=Date.now()) {
  const distance=350-((now-demo.startedAt)/1000*14)%340;
  return demo.navigator.update({x:0,z:distance,y:0,heading:0,speed:30},now);
}
module.exports={makeDemo,demoState};
