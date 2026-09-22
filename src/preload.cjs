const {contextBridge,ipcRenderer}=require('electron');
contextBridge.exposeInMainWorld('laneGuide',{
 getState:()=>ipcRenderer.invoke('state:get'),
 getCities:()=>ipcRenderer.invoke('cities:get'),
 action:(name,value)=>ipcRenderer.invoke('action',name,value),
 onState:callback=>{const listener=(_event,state)=>callback(state);ipcRenderer.on('state',listener);return ()=>ipcRenderer.removeListener('state',listener);}
});
