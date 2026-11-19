import {createServer} from 'node:http';
import {readFileSync,writeFileSync} from 'node:fs';
import {resolve,extname} from 'node:path';
const build=resolve(process.argv[2]),report=process.argv[3];
const {chromium}=await import(new URL('file://'+process.env.HOME+'/repos/playwright/node_modules/playwright/index.mjs'));
const here=new URL('.',import.meta.url).pathname;
const server=createServer((req,res)=>{try {
 const name=req.url.slice(1).split('?')[0];
 if(name===''){res.setHeader('Content-Type','text/html');res.end('<!doctype html><meta name="viewport" content="width=device-width"><h1>Bounded Biology Worker smoke test</h1>');return}
 if(!['worker.mjs','client.mjs','bio_worker.mjs','bio_worker.wasm'].includes(name))throw Error('missing');
 res.setHeader('Content-Type',name.endsWith('.wasm')?'application/wasm':'text/javascript');
 res.end(readFileSync(resolve(name.startsWith('bio_worker')?build:here,name)));
 }catch{res.statusCode=404;res.end('missing')}});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const origin='http://127.0.0.1:'+server.address().port,browser=await chromium.launch({headless:true});const results=[];
try {
 for(const width of [1280,390]){
  const page=await browser.newPage({viewport:{width,height:844}}),requests=[],errors=[];
  page.on('request',r=>requests.push(r.url()));page.on('pageerror',e=>errors.push(e.message));await page.goto(origin);
  const result=await page.evaluate(async()=>{
   const {runBio}=await import('/client.mjs');let checks=0;const ok=(value,name)=>{if(!value)throw Error(name);checks++};
   const req={id:1,model:'decay',parameters:[1,.5],horizon:2,points:21};
   const decay=await runBio(req);ok(Math.abs(decay.values.at(-2)-Math.exp(-1))<1e-7,'decay');
   ok(decay.engine==='CVODE BDF'&&decay.columns.join(',')==='time,A,B','metadata');
   const enz=await runBio({id:2,model:'enzyme',parameters:[.01,1,10,5,5,0],horizon:2,points:81,tighter:true});
   ok(Math.abs(enz.values.at(-1)-.0480600720326477)<1e-8,'enzyme');
   for(let i=0;i<81;i++)ok(Math.abs(enz.values[5*i+1]+enz.values[5*i+3]-.01)<1e-9&&Math.abs(enz.values[5*i+2]+enz.values[5*i+3]+enz.values[5*i+4]-1)<1e-9,'conservation');
   const ssa={id:3,model:'birth-death',parameters:[0,6,2],horizon:2,points:21,seed:123};
   const a=await runBio(ssa),b=await runBio(ssa);ok(JSON.stringify(Array.from(a.values))===JSON.stringify(Array.from(b.values)),'seed reproducibility');
   for(const request of [{...req,points:258},{...req,model:'constructor'},{...req,parameters:[-1,.5]},{...req,path:'/private'},{...ssa,parameters:[1.5,6,2]}]){
    let rejected=false;try{await runBio(request)}catch{rejected=true}ok(rejected,'reject invalid request');
   }
   let timed=false;try{await runBio(req,{timeout:1})}catch(e){timed=/time limit/.test(e.message)}ok(timed,'hard timeout');
   const ctrl=new AbortController();ctrl.abort();let aborted=false;try{await runBio(req,{signal:ctrl.signal})}catch(e){aborted=/cancelled/.test(e.message)}ok(aborted,'cancellation');
   return {checks,pass:true};
  });
  if(errors.length||requests.some(u=>!u.startsWith(origin)))throw Error('page error or external request');
  results.push({width,...result,externalRequests:0,pageErrors:0});console.log('PASS Worker @'+width+': '+result.checks+' assertions');await page.close();
 }
} finally {await browser.close();await new Promise(r=>server.close(r))}
writeFileSync(report,JSON.stringify({pass:true,results},null,2)+'\n');
