// One request per disposable Worker: no retained model/seed/state and hard cancellation.
export function runBio(request, {timeout=5000, signal}={}) {
  if (!Number.isFinite(timeout)||timeout<1||timeout>10000) return Promise.reject(Error('timeout must be in [1,10000] ms'));
  return new Promise((resolve,reject)=>{
    const worker=new Worker(new URL('./worker.mjs',import.meta.url),{type:'module'});
    let timer;
    const done=(fn,value)=>{clearTimeout(timer);signal?.removeEventListener('abort',abort);worker.terminate();fn(value)};
    const abort=()=>done(reject,Error('cancelled'));
    if(signal?.aborted){abort();return}
    signal?.addEventListener('abort',abort,{once:true});
    timer=setTimeout(()=>done(reject,Error('time limit exceeded')),timeout);
    worker.onerror=event=>done(reject,Error(event.message));
    worker.onmessage=({data})=>{if(data.id!==request.id){done(reject,Error('request id mismatch'));return}data.ok?done(resolve,data):done(reject,Error(data.error))};
    worker.postMessage(request);
  });
}
