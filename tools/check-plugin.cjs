const dgram=require('node:dgram'),{spawn}=require('node:child_process'),path=require('node:path'),assert=require('node:assert/strict');
const socket=dgram.createSocket('udp4'),packets=[];let finished=false;
const timeout=setTimeout(()=>finish(Error('Native telemetry test timed out')),8000);
function finish(error){if(finished)return;finished=true;clearTimeout(timeout);socket.close();if(error){console.error(error);process.exitCode=1;}else console.log('PASS: compiled DLL emits valid position, heading, speed, pause and disconnect packets over localhost UDP.');}
socket.on('error',finish);socket.on('message',b=>{try{packets.push(JSON.parse(b.toString()));}catch{}});
socket.bind(37539,'127.0.0.1',()=>{
 const child=spawn(path.resolve('output/telemetry_host.exe'),[path.resolve('native/ets2_lane_guide.dll')],{windowsHide:true});
 child.stdout.pipe(process.stdout);child.stderr.pipe(process.stderr);child.on('error',finish);
 child.on('exit',code=>setTimeout(()=>{try{
  assert.equal(code,0);const active=packets.find(p=>p.connected&&!p.paused&&p.placed);assert(active);assert.equal(active.x,1234.5);assert.equal(active.z,-9876.5);assert.equal(active.speed,25);assert.equal(active.heading,.75);
  assert(packets.some(p=>p.paused));assert(packets.some(p=>p.connected===false));finish();
 }catch(error){finish(error);}},100));
});
