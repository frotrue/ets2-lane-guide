'use strict';

// Semaphore IDs are logical groups, NOT indexes into the locator array.
function approachSignals(description, options, representative) {
  if (!options.length) return null;
  const groups=new Map();
  let stop=null;
  for (const option of options) {
    const controlled=option.chain.map(i=>description.navCurves[i]).find(c=>Number.isInteger(c?.semaphoreId)&&c.semaphoreId>=0);
    // Without knowing the driver's lane, an uncontrolled alternative is ambiguous.
    if (!controlled) return null;
    const id=controlled.semaphoreId;
    const locators=(description.semaphores||[]).filter(s=>s.id===id&&[0,2,3,4].includes(s.type)&&[s.x,s.y,s.z].every(Number.isFinite));
    if (!locators.length) return null;
    groups.set(id,{id,locators});
    if (option===representative) stop=controlled.start;
  }
  return stop?{groups:[...groups.values()],stop}:null;
}

module.exports={approachSignals};
