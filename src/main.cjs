'use strict';
const {app,BrowserWindow,ipcMain,globalShortcut,screen,Menu,Tray,nativeImage}=require('electron');
const fs=require('node:fs'),path=require('node:path'),dgram=require('node:dgram');
const {Telemetry}=require('./telemetry.cjs');
const {RoadMap,Navigator}=require('./navigation.cjs');
const {makeDemo,demoState}=require('./demo.cjs');
const {trafficSignalState}=require('./traffic-signals.cjs');
const {TrafficSignalReceiver}=require('./traffic-signal-receiver.cjs');
const {startSignalBridge}=require('./traffic-signal-bridge.cjs');
const root=path.join(__dirname,'..');
let overlay,panel,tray,map,navigator,socket,timer,signalBridge,quitting=false,networkError='',mapError='';
let mode=process.argv.includes('--demo')?'demo':'live',demo=makeDemo(),locked=false;
let settings={opacity:96,scale:100,position:null},shortcuts=[];
const telemetry=new Telemetry();
const signalReceiver=new TrafficSignalReceiver();
if(!app.requestSingleInstanceLock()){app.quit();}else{
app.on('second-instance',()=>panel?.show());
app.whenReady().then(start).catch(error=>{console.error(error);app.quit();});
}
function configPath(){return path.join(app.getPath('userData'),'settings.json');}
function saveSettings(){try{fs.writeFileSync(configPath(),JSON.stringify(settings,null,2));}catch(error){console.error('Settings:',error.message);}}
function configureWindow(win){win.webContents.setWindowOpenHandler(()=>({action:'deny'}));win.webContents.on('will-navigate',e=>e.preventDefault());win.webContents.on('will-attach-webview',e=>e.preventDefault());}
function emit(){const state=getState();for(const win of [overlay,panel])if(win&&!win.isDestroyed())win.webContents.send('state',state);}
function getState(){
 const t=telemetry.get();
 if(!t||!t.placed||t.paused)signalReceiver.clear();
 let guidance;
 if(mode==='demo')guidance=demoState(demo);
 else if(networkError)guidance={status:'error',message:'연결 포트를 열지 못했습니다',detail:networkError};
 else if(!t)guidance={status:'disconnected',message:'ETS2 연결 대기 중',detail:'게임에서 주행을 시작해 주세요'};
 else if(!t.placed||t.paused)guidance={status:'paused',message:'게임이 일시 정지되었습니다',detail:'주행 화면으로 돌아오면 안내합니다'};
 else if(!navigator)guidance={status:'error',message:'지도 데이터를 불러오지 못했습니다',detail:mapError};
 else guidance=navigator.update(t);
 return {mode,scenario:demo.scenario,locked,settings,shortcuts,guidance,connected:!!t,paused:!!t?.paused,
  trafficSignal:mode==='demo'?trafficSignalState({mode,demoStartedAt:demo.startedAt}):
   (mode==='live'&&t?.placed&&!t.paused?signalReceiver.select(guidance.signalTargets,{gameVersion:map?.data.gameVersion}):null),
  speed:t?Math.round(Math.abs(t.speed)*3.6):null,position:t?.placed?{x:Math.round(t.x),z:Math.round(t.z)}:null,
  map:map?{version:map.data.gameVersion,...map.data.stats,cities:map.data.cities.length}:null,mapError,networkError,
  destination:navigator?.destination?.name||null,routeActive:!!navigator?.path.length};
}
function setLocked(value){locked=!!value;overlay.setIgnoreMouseEvents(locked,{forward:true});overlay.setFocusable(!locked);emit();}
function showOverlay(){overlay.showInactive();overlay.setAlwaysOnTop(true,'screen-saver');}
async function start(){
 app.setAppUserModelId('local.ets2.laneguide');
 try{const loaded=JSON.parse(fs.readFileSync(configPath(),'utf8'));settings.opacity=Math.max(55,Math.min(100,Number(loaded.opacity)||96));settings.scale=Math.max(80,Math.min(140,Number(loaded.scale)||100));if(loaded.position&&Number.isFinite(loaded.position.x)&&Number.isFinite(loaded.position.y))settings.position=loaded.position;}catch{}
 const work=screen.getPrimaryDisplay().workArea;
 const initial=settings.position||{x:work.x+Math.round(work.width/2)-224,y:work.y+34};
 const inside=screen.getAllDisplays().some(d=>initial.x>=d.workArea.x&&initial.x+448<=d.workArea.x+d.workArea.width&&initial.y>=d.workArea.y&&initial.y+258<=d.workArea.y+d.workArea.height);
 const pos=inside?initial:{x:work.x+Math.round(work.width/2)-224,y:work.y+34};
 const webPreferences={preload:path.join(__dirname,'preload.cjs'),contextIsolation:true,nodeIntegration:false,sandbox:true,backgroundThrottling:false};
 overlay=new BrowserWindow({...pos,width:448,height:258,frame:false,transparent:true,resizable:false,maximizable:false,fullscreenable:false,alwaysOnTop:true,skipTaskbar:true,show:false,hasShadow:false,webPreferences});
 panel=new BrowserWindow({width:860,height:Math.min(900,work.height-70),minWidth:760,minHeight:640,title:'Lane Guide · ETS2',backgroundColor:'#f4f5f2',autoHideMenuBar:true,show:false,webPreferences});
 for(const win of [overlay,panel])configureWindow(win);
 overlay.setAlwaysOnTop(true,'screen-saver');overlay.setVisibleOnAllWorkspaces(true,{visibleOnFullScreen:true});
 overlay.setOpacity(settings.opacity/100);overlay.webContents.setZoomFactor(settings.scale/100);overlay.setSize(Math.round(448*settings.scale/100),Math.round(258*settings.scale/100));
 overlay.on('moved',()=>{const b=overlay.getBounds();settings.position={x:b.x,y:b.y};saveSettings();});
 panel.on('close',e=>{if(!quitting){e.preventDefault();panel.hide();}});
 overlay.on('close',e=>{if(!quitting){e.preventDefault();overlay.hide();}});
 ipcMain.handle('state:get',()=>getState());
 ipcMain.handle('cities:get',()=>map?.data.cities||[]);
 ipcMain.handle('action',(_event,name,value)=>handleAction(name,value));
 await Promise.all([overlay.loadFile(path.join(root,'ui/overlay.html')),panel.loadFile(path.join(root,'ui/panel.html'))]);
 const icon=nativeImage.createFromDataURL('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAHUlEQVQ4T2Nk+M/wn4ECwESJ5lEDRg0YNWAwGQAAzfgf8Z5GRGsAAAAASUVORK5CYII=');
 tray=new Tray(icon);tray.setToolTip('ETS2 Lane Guide');
 tray.setContextMenu(Menu.buildFromTemplate([{label:'설정 열기',click:()=>panel.show()},{label:'안내창 표시',click:showOverlay},{label:'클릭 통과 전환',click:()=>setLocked(!locked)},{type:'separator'},{label:'종료',click:()=>app.quit()}]));
 tray.on('double-click',()=>panel.show());
 for(const [key,action] of [['F8',()=>setLocked(!locked)],['F9',()=>overlay.isVisible()?overlay.hide():showOverlay()],['F10',()=>panel.isVisible()?panel.hide():panel.show()]]){
  shortcuts.push({key,registered:globalShortcut.register(key,action)});
 }
 showOverlay();panel.show();
 socket=dgram.createSocket('udp4');socket.on('message',message=>telemetry.ingest(message));socket.on('error',error=>{networkError=error.code==='EADDRINUSE'?'37539 포트를 다른 프로그램이 사용 중입니다.':error.message;emit();});socket.bind(37539,'127.0.0.1');
 if(process.platform==='win32')signalBridge=startSignalBridge(path.join(root,'native/traffic_signal_reader.exe'),signalReceiver);
 // Defer loading so the control window opens promptly.
 try{const data=JSON.parse(await fs.promises.readFile(path.join(root,'data/map.json'),'utf8'));map=new RoadMap(data);navigator=new Navigator(map);}catch(error){mapError=error.message;}
 timer=setInterval(emit,200);emit();
}
async function handleAction(name,value){
 try{
  switch(name){
   case 'mode':if(!['demo','live'].includes(value))throw Error('잘못된 모드입니다.');mode=value;if(mode==='demo')demo=makeDemo(demo.scenario);break;
   case 'scenario':if(!['left','right','straight'].includes(value))throw Error('잘못된 시나리오입니다.');demo=makeDemo(value);mode='demo';break;
   case 'route':{const t=telemetry.get();if(!t||!t.placed)throw Error('먼저 ETS2에서 일반 도로에 진입해 주세요.');if(!navigator)throw Error('지도를 불러오는 중입니다.');const city=map.data.cities.find(c=>c.token===value);if(!city)throw Error('목적지를 선택해 주세요.');navigator.start(t,city);mode='live';break;}
   case 'clear-route':navigator?.clear();break;
   case 'lock':setLocked(value);break;
   case 'show':showOverlay();break;
   case 'panel':panel.show();break;
   case 'hide':overlay.hide();break;
   case 'opacity':settings.opacity=Math.max(55,Math.min(100,Number(value)||96));overlay.setOpacity(settings.opacity/100);saveSettings();break;
   case 'scale':settings.scale=Math.max(80,Math.min(140,Number(value)||100));overlay.webContents.setZoomFactor(settings.scale/100);overlay.setSize(Math.round(448*settings.scale/100),Math.round(258*settings.scale/100));saveSettings();break;
   case 'reset-position':{const w=screen.getPrimaryDisplay().workArea;overlay.setPosition(w.x+Math.round(w.width/2)-Math.round(overlay.getSize()[0]/2),w.y+34);showOverlay();break;}
   case 'quit':app.quit();break;
   default:throw Error('지원하지 않는 동작입니다.');
  }
  emit();return {ok:true};
 }catch(error){return {ok:false,error:error.message};}
}
app.on('before-quit',()=>{quitting=true;clearInterval(timer);signalBridge?.stop();globalShortcut.unregisterAll();try{socket?.close();}catch{};});
app.on('window-all-closed',()=>app.quit());
