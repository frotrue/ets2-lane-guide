'use strict';
function arrow(directions=['straight'], highlighted=null) {
 const paths={
  straight:['M24 53V16','24,5 12,23 36,23'],
  left:['M24 53V32L8 16','5,10 5,29 24,10'],
  right:['M24 53V32L40 16','43,10 24,10 43,29'],
  uturn:['M34 53V23Q34 9 21 9Q8 9 8 23V31','8,40 0,27 16,27']
 };
 // Draw the selected branch last so its shared stem stays highlighted.
 const ordered=[...directions].sort((a,b)=>Number(highlighted?.includes(a))-Number(highlighted?.includes(b)));
 return `<svg viewBox="0 0 48 60" fill="none" aria-hidden="true">${ordered.map(d=>{const p=paths[d]||paths.straight;return `<g class="${highlighted&&!highlighted.includes(d)?'secondary':'branch'}"><path d="${p[0]}" stroke="currentColor" stroke-width="6.5" stroke-linecap="square" stroke-linejoin="miter"/><polygon points="${p[1]}" fill="currentColor"/></g>`;}).join('')}</svg>`;
}
function render(state) {
 const g=state.guidance||{},active=g.status==='guidance',demo=state.mode==='demo';
 document.getElementById('guide').className=`guide ${active?'':'waiting'} ${g.distance<70&&active?'urgent':''} ${state.locked?'locked':''}`;
 const badge=document.getElementById('badge');badge.textContent=demo?'데모 · 실제 주행 아님':state.connected?'실시간 · 시험판':'연결 대기';badge.className=`badge ${demo?'demo':state.connected?'live':''}`;
 // A synthetic countdown must never appear in live mode.
 const signal=state.trafficSignal,signalView=document.getElementById('traffic-signal');
 const sourceMatches=demo?signal?.source==='demo':state.connected&&!state.paused&&signal?.source==='live'&&g.signalTargets?.length;
 const showSignal=sourceMatches&&['red','amber','green'].includes(signal.state)&&Number.isInteger(signal.remainingSeconds)&&signal.remainingSeconds>0;
 signalView.hidden=!showSignal;
 if(showSignal){signalView.dataset.state=signal.state;document.getElementById('signal-seconds').textContent=signal.remainingSeconds;document.getElementById('signal-label').textContent=demo?'시연 신호':signal.label||'신호 전환까지';signalView.setAttribute('aria-label',`${demo?'시연 신호':'신호 전환'}: ${{red:'빨간불',amber:'노란불',green:'초록불'}[signal.state]}, ${signal.remainingSeconds}초 남음`);}else{signalView.removeAttribute('aria-label');document.getElementById('signal-seconds').textContent='';}
 const signalOnly=showSignal&&!active&&Number.isFinite(g.signalDistance);
 if(signalOnly)document.getElementById('guide').classList.remove('waiting');
 document.getElementById('message').textContent=g.message||'준비 중';
 if(signalOnly)document.getElementById('message').textContent='앞 신호등';
 const distance=document.getElementById('distance');distance.replaceChildren();
 if(active||signalOnly){const number=document.createElement('strong');number.textContent=String(Math.max(10,Math.round((active?g.distance:g.signalDistance)/10)*10));distance.append(number,'m');}else distance.textContent=g.destination||'차선 안내';
 document.getElementById('maneuver-icon').innerHTML=active?arrow([g.turn]):g.status==='arrived'?'✓':arrow(['straight']);
 if(signalOnly)document.getElementById('maneuver-icon').innerHTML='<svg viewBox="0 0 48 60" fill="none" aria-hidden="true"><rect x="12" y="3" width="24" height="53" rx="7" stroke="currentColor" stroke-width="3"/><circle cx="24" cy="15" r="5" fill="currentColor"/><circle cx="24" cy="30" r="5" fill="currentColor"/><circle cx="24" cy="45" r="5" fill="currentColor"/></svg>';
 const row=document.getElementById('lanes');
 const signature=JSON.stringify([g.lanes,g.recommended,g.turn,active]);
 if(row.dataset.signature!==signature){row.dataset.signature=signature;row.replaceChildren();if(active)g.lanes.forEach((dirs,i)=>{const lane=document.createElement('div');const selected=g.recommended.includes(i);lane.className=`lane ${selected?'active':''}`;lane.innerHTML=arrow(dirs,selected?(dirs.includes(g.turn)?[g.turn]:dirs):null);lane.setAttribute('aria-label',`왼쪽부터 ${i+1}번째 차로${selected?' 추천':''}`);row.append(lane);});}
 const empty=document.getElementById('empty');empty.hidden=active;empty.style.display=active?'none':'flex';empty.textContent=g.detail||'운전 중에는 전방과 도로 표지판을 확인하세요';
 document.getElementById('detail').textContent=active?`${g.detail} · ${g.destination||'별도 경로'}`:demo?'시연 경로':g.destination?'별도로 계산한 경로':'독립 경로 안내';
 document.getElementById('lock-hint').textContent=state.locked?'F8 · 위치 변경':'F8 · 위치 고정';
}
const preview=new URLSearchParams(location.search).has('preview');
if(window.laneGuide){window.laneGuide.onState(render);window.laneGuide.getState().then(render);document.getElementById('settings').onclick=()=>window.laneGuide.action('panel');}
else if(preview){render({mode:'demo',trafficSignal:{source:'demo',state:'green',remainingSeconds:12},guidance:{status:'guidance',distance:280,message:'오른쪽 2개 차로로',detail:'오른쪽 방향',destination:'시연 경로',turn:'right',lanes:[['straight'],['straight','right'],['right']],recommended:[1,2]}});}
