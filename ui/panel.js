'use strict';
const $=id=>document.getElementById(id);let cities=[],lastCityCount=0,loadingCities=false;
const api=window.laneGuide;
async function action(name,value){if(!api)return;const result=await api.action(name,value);$('error').hidden=result.ok;if(!result.ok)$('error').textContent=result.error;}
function cityOptions(){const selected=$('city').value,query=$('city-search').value.toLocaleLowerCase();$('city').replaceChildren();const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent='목적지 도시 선택';$('city').append(placeholder);for(const city of cities.filter(c=>(c.name+' '+c.token+' '+c.country).toLocaleLowerCase().includes(query))){const option=document.createElement('option');option.value=city.token;option.textContent=`${city.name} · ${city.country.toUpperCase()}`;$('city').append(option);}$('city').value=selected;if(!$('city').value&&$('city').options.length===2)$('city').selectedIndex=1;}
function render(s){
 const demo=s.mode==='demo';$('live').classList.toggle('selected',!demo);$('demo').classList.toggle('selected',demo);$('live-content').hidden=demo;$('demo-content').hidden=!demo;
 $('connection').textContent=s.connected?(s.paused?'ETS2 일시 정지':`ETS2 연결됨 · ${s.speed} km/h`):'ETS2 연결 대기';$('connection-dot').classList.toggle('connected',s.connected);
 $('lock').classList.toggle('chosen',s.locked);$('lock').textContent=s.locked?'고정됨 · 클릭 통과 켜짐':'위치 고정 / 클릭 통과';$('lock').dataset.locked=s.locked;
 for(const key of ['opacity','scale']){if(document.activeElement!==$(key))$(key).value=s.settings[key];$(key+'-value').textContent=s.settings[key]+'%';}
 document.querySelectorAll('[data-scenario]').forEach(b=>b.classList.toggle('chosen',b.dataset.scenario===s.scenario));
 $('start-route').disabled=!s.connected||!s.map;$('clear-route').hidden=!s.routeActive;$('route-status').textContent=s.destination?`${s.destination} 방향 안내 중 · ${s.guidance.message}`:'게임에서 일반 도로에 진입한 뒤 시작하세요.';
 $('map-status').textContent=s.map?`ETS2 ${s.map.version} · ${s.map.cities}개 도시 · 로컬 지도`:s.mapError?'지도 데이터 오류':'지도 데이터 확인 중';
 const failed=s.shortcuts?.filter(k=>!k.registered)||[];$('shortcut-warning').hidden=!failed.length;$('shortcut-warning').textContent=failed.map(k=>k.key).join(', ')+' 단축키를 다른 앱이 사용 중입니다. 위 버튼으로 조작하세요.';
 if(s.map&&s.map.cities!==lastCityCount&&!loadingCities){loadingCities=true;api.getCities().then(result=>{cities=result;lastCityCount=result.length;cityOptions();}).finally(()=>loadingCities=false);}
}
$('live').onclick=()=>action('mode','live');$('demo').onclick=()=>action('mode','demo');$('city-search').oninput=cityOptions;
$('start-route').onclick=()=>action('route',$('city').value);$('clear-route').onclick=()=>action('clear-route');
$('opacity').oninput=()=>action('opacity',Number($('opacity').value));$('scale').oninput=()=>action('scale',Number($('scale').value));
$('lock').onclick=()=>action('lock',$('lock').dataset.locked!=='true');$('show').onclick=()=>action('show');$('reset').onclick=()=>action('reset-position');$('quit').onclick=()=>action('quit');
document.querySelectorAll('[data-scenario]').forEach(b=>b.onclick=()=>action('scenario',b.dataset.scenario));
if(api){api.onState(render);api.getState().then(render);}else{$('map-status').textContent='브라우저 디자인 미리보기';$('start-route').disabled=true;}
