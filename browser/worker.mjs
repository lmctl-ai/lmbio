// Fixed teaching models only. The caller terminates this Worker on timeout/cancel.
import createBio from './bio_worker.mjs';
let module;
self.onmessage = async ({data: request}) => {
  const id = request?.id;
  try {
    if (!request || typeof request !== 'object' || Array.isArray(request)) throw Error('object request required');
    const allowed = new Set(['id','model','parameters','horizon','points','seed','tighter']);
    if (Object.keys(request).some(k=>!allowed.has(k))) throw Error('unknown request field');
    if (!Number.isSafeInteger(id) || id < 0) throw Error('integer request id required');
    const models = {decay:{kind:0,n:2,columns:['time','A','B']}, enzyme:{kind:1,n:6,columns:['time','E','S','ES','P']}, 'birth-death':{kind:2,n:3,columns:['time','N']}};
    if (!Object.hasOwn(models,request.model)) throw Error('unknown model');
    const spec = models[request.model], p = request.parameters;
    if (!Array.isArray(p) || p.length !== spec.n || p.some(x=>typeof x!=='number'||!Number.isFinite(x))) throw Error('invalid parameters');
    if (typeof request.horizon!=='number'||!Number.isFinite(request.horizon)||request.horizon<=0||request.horizon>20 || !Number.isInteger(request.points)||request.points<2||request.points>257) throw Error('invalid grid');
    const seed = request.seed ?? 0;
    if (!Number.isInteger(seed)||seed<0||seed>4294967295 || (request.tighter!==undefined&&typeof request.tighter!=='boolean')) throw Error('invalid seed/tolerance');
    module ??= await createBio();
    const args = [...p,...Array(6-p.length).fill(0)];
    const rows = module._bio_run(spec.kind,...args,request.horizon,request.points,seed,request.tighter?1:0);
    if (rows<0) throw Error(module.UTF8ToString(module._bio_error()));
    const width = module._bio_width();
    if(width!==spec.columns.length) throw Error('output dimensions');
    const values = module.HEAPF64.slice(module._bio_output()/8,module._bio_output()/8+rows*width);
    self.postMessage({id,ok:true,engine:spec.kind===2?'Gillespie SSA':'CVODE BDF',columns:spec.columns,rows,values},[values.buffer]);
  } catch(e) { self.postMessage({id,ok:false,error:String(e.message).slice(0,256)}); }
};
