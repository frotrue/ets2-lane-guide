'use strict';
const fs=require('node:fs'),path=require('node:path');
const root=path.join(__dirname,'..');
function encodeMap(data){
 const chunks=[Buffer.from('LNGMAP02')],nodes=new Map();
 const node=s=>{if(!nodes.has(s))nodes.set(s,nodes.size);return nodes.get(s);};
 for(const e of data.edges){node(e.from);node(e.to);}
 const u8=n=>{const b=Buffer.alloc(1);b.writeUInt8(n);chunks.push(b);};
 const u32=n=>{const b=Buffer.alloc(4);b.writeUInt32LE(n);chunks.push(b);};
 const uid=s=>{const b=Buffer.alloc(8);b.writeBigUInt64LE(BigInt('0x'+s.split(':')[0]));chunks.push(b);};
 const f64=n=>{if(!Number.isFinite(n))throw Error('Non-finite map coordinate');const b=Buffer.alloc(8);b.writeDoubleLE(n);chunks.push(b);};
 const str=s=>{const b=Buffer.from(s||'');u32(b.length);chunks.push(b);};
 const mask=dirs=>dirs.reduce((n,d)=>n|({straight:1,left:2,right:4,uturn:8}[d]||0),0);
 str(data.gameVersion);u32(data.cities.length);
 for(const c of data.cities){str(c.token);str(c.name);str(c.country);f64(c.x);f64(c.y);}
 u32(nodes.size);u32(data.edges.length);
 for(const e of data.edges){
  u8({road:0,junction:1,join:2}[e.kind]);u8(e.lanes||0);u8(e.continuationLanes||0);u8(mask([e.navigationTurn||e.guide?.turn]));
  u32(node(e.from));u32(node(e.to));uid(e.from);uid(e.to);u32(e.points.length);
  for(const p of e.points)for(const v of [p[0],p[1],p[2]||0])f64(v);
  u8(e.guide?.lanes.length||0);for(const lane of e.guide?.lanes||[])u8(mask(lane));
  u8((e.guide?.recommended||[]).reduce((n,i)=>n|(1<<i),0));
  u32(e.signalTargets?.length||0);
  for(const t of e.signalTargets||[]){u32(t.id);u32(t.positions.length);for(const p of t.positions){f64(p.x);f64(p.y);f64(p.z);}}
  f64(e.signalStopAlong||0);
 }
 return Buffer.concat(chunks);
}
if(require.main===module){
 const data=JSON.parse(fs.readFileSync(path.join(root,'data/map.json'),'utf8'));
 const bytes=encodeMap(data),target=path.join(root,'data/ingame-map.bin');
 fs.writeFileSync(target+'.tmp',bytes);fs.renameSync(target+'.tmp',target);
 console.log(JSON.stringify({edges:data.edges.length,cities:data.cities.length,bytes:bytes.length,target}));
}
module.exports={encodeMap};
