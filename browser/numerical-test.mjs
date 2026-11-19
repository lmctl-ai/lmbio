import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {pathToFileURL} from 'node:url';
const dir=process.argv[2];
const create=(await import(pathToFileURL(dir+'/bio_worker.mjs'))).default;
const m=await create({wasmBinary:readFileSync(dir+'/bio_worker.wasm')});
assert.equal(m._bio_limit(),256*1024*1024);
function run(kind,p,horizon=2,points=21,seed=0,tight=0){const rows=m._bio_run(kind,...[...p,...Array(6-p.length).fill(0)],horizon,points,seed,tight);if(rows<0)throw Error(m.UTF8ToString(m._bio_error()));return Array.from(m.HEAPF64.slice(m._bio_output()/8,m._bio_output()/8+rows*m._bio_width()))}
const coarse=run(0,[1,.5]),fine=run(0,[1,.5],2,21,0,1),target=Math.exp(-1);
assert.ok(Math.abs(fine.at(-2)-target)<1e-9);assert.ok(Math.abs(fine.at(-2)-target)<Math.abs(coarse.at(-2)-target));
for(let i=0;i<21;i++)assert.ok(Math.abs(fine[i*3+1]+fine[i*3+2]-1)<1e-9);
const enzyme=run(1,[.01,1,10,5,5,0],2,81,0,1);
for(let i=0;i<81;i++){const p=enzyme.slice(i*5,i*5+5);assert.ok(Math.abs(p[1]+p[3]-.01)<1e-9);assert.ok(Math.abs(p[2]+p[3]+p[4]-1)<1e-9)}
assert.ok(Math.abs(enzyme.at(-1)-.0480600720326477)<1e-8);
assert.deepEqual(run(2,[0,6,2],2,21,123),run(2,[0,6,2],2,21,123));
let total=0;for(let seed=0;seed<256;seed++){const v=run(2,[0,6,2],2,2,seed).at(-1);assert.ok(Number.isInteger(v)&&v>=0);total+=v}
const mean=3*(1-Math.exp(-4));assert.ok(Math.abs(total/256-mean)<8*Math.sqrt(mean/256));
for(const f of [()=>run(2,[1.5,6,2]),()=>run(0,[1,.5],21),()=>run(0,[1,.5],2,258),()=>run(99,[1])])assert.throws(f);
console.log(JSON.stringify({pass:true,allocator:m._bio_limit(),decay_error:Math.abs(fine.at(-2)-target),enzyme_final:enzyme.at(-1),ssa_seeds:256,ssa_mean:total/256,analytic_mean:mean}));
