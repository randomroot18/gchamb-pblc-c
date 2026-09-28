// Runs the delivered embedded script with a DOM/fetch/timer harness; no browser dependency.
const fs=require('fs'),vm=require('vm'),path=require('path'),assert=require('assert');
const source=fs.readFileSync(path.join(__dirname,'../ShunyaUjjain/web_ui.h'),'utf8');
const script=source.split('<script>')[1].split('</script>')[0],html=source.split('<script>')[0];
const ids=new Map();let document,clock=0,seq=0;const timers=new Map(),intervals=[];
class E{
 constructor(tag='div'){this.tagName=tag;this.children=[];this.dataset={};this.style={};this.attrs={};this.events={};this.value='';this.hidden=false;this._text='';this.width=900;this.height=230}
 set id(v){this.attrs.id=v;ids.set(v,this)}get id(){return this.attrs.id}
 set className(v){this.attrs.class=v}get className(){return this.attrs.class||''}
 get classList(){const el=this;return {contains(c){return el.className.split(' ').includes(c)},toggle(c,on){const set=new Set(el.className.split(' ').filter(Boolean));if(on===undefined)on=!set.has(c);on?set.add(c):set.delete(c);el.className=[...set].join(' ');return on},add(c){this.toggle(c,true)},remove(c){this.toggle(c,false)}}}
 set textContent(v){this._text=String(v);this.children=[]}get textContent(){return this._text+this.children.map(c=>c.textContent).join('')}
 set innerHTML(v){this.children=[];parse(v,this)}
 setAttribute(k,v){this.attrs[k]=String(v);if(k==='id')this.id=v;if(k==='class')this.className=v;if(k==='value')this.value=v;if(k==='hidden')this.hidden=true;if(k.startsWith('data-'))this.dataset[k.slice(5).replace(/-([a-z])/g,(_,x)=>x.toUpperCase())]=v}
 getAttribute(k){return this.attrs[k]}
 append(...nodes){for(const n of nodes){n.parent=this;this.children.push(n)}}replaceChildren(...nodes){this.children=[];this._text='';this.append(...nodes)}
 querySelectorAll(sel){const parts=sel.split(' '),last=parts.pop();const match=(e,s)=>s.startsWith('.')?e.classList.contains(s.slice(1)):s.startsWith('#')?e.id===s.slice(1):e.tagName===s;
  const all=[];function walk(e){for(const c of e.children){if(match(c,last)){let p=c.parent,ok=true;for(const part of [...parts].reverse()){while(p&&!match(p,part))p=p.parent;if(!p){ok=false;break}p=p.parent}if(ok)all.push(c)}walk(c)}}walk(this);return all}
 querySelector(s){return this.querySelectorAll(s)[0]||null}addEventListener(k,fn){this.events[k]=fn}focus(){document.activeElement=this}
 getBoundingClientRect(){return {left:0,top:0,width:240,height:240}}setPointerCapture(){}releasePointerCapture(){}
 checkValidity(){const v=Number(this.value);return this.value!==''&&Number.isFinite(v)&&(this.min===undefined||v>=Number(this.min))&&(this.max===undefined||v<=Number(this.max))}
 getContext(){return new Proxy({},{get:()=>()=>{}})}
}
function parse(markup,parent){const stack=[parent];for(const tok of markup.matchAll(/<\/?[a-zA-Z][^>]*>|[^<]+/g)){const s=tok[0];if(s.startsWith('</')){const tag=s.slice(2,-1).trim();for(let i=stack.length-1;i>0;i--)if(stack[i].tagName===tag){stack.length=i;break}continue}if(s.startsWith('<')){const tag=s.match(/^<([\w-]+)/)[1];const e=new E(tag);for(const a of s.matchAll(/([\w-]+)(?:="([^"]*)"|='([^']*)')?/g)){if(a.index===1)continue;e.setAttribute(a[1],a[2]??a[3]??'')}stack.at(-1).append(e);if(!['meta','input','br','hr','link','path','circle'].includes(tag)&&!s.endsWith('/>'))stack.push(e)}else stack.at(-1)._text+=s}}
const root=new E('document');parse(html.replace(/<style>[\s\S]*?<\/style>/,''),root);
document={activeElement:null,getElementById(id){assert(ids.has(id),'Missing element '+id);return ids.get(id)},createElement(tag){return new E(tag)},querySelectorAll:s=>root.querySelectorAll(s)};
const $=id=>document.getElementById(id);$('historyRange').value='1h';
const cfg={temperature_target:27,humidity_target:85,humidity_reducer:'driest',heater:'AUTO',humidifier:'AUTO',mist:'AUTO',exhaust:'AUTO'};
const data={name:'Ujjain',version:'1.0.2-ujjain',state:'IDLE',reason:'Waiting',faults:[],advisories:[],notice:'',banner_message:{text:'',kind:''},top:{valid:true,temperature:27,rh:80},bottom:{valid:true,temperature:26,rh:75},average:26.5,rh_low:75,rh_high:80,outputs:{heater:false,humidifier:false,mist:false,exhaust:false},config:cfg,blocked_by:{heater:null,humidifier:null,mist:null,exhaust:null},mist_next_sec:4350,wifi:{connected:true,ip:'192.168.1.2',hostname:'shunya-238C',ssid:'lab'},ota:{result:'Idle',available:false},uptime_ms:120000};
const calls=[];let unauthorized=false,holdPost=null;
const fetch=async(p,opt={})=>{calls.push([p,opt]);if(opt.method==='POST'&&unauthorized)return {status:401,ok:false,json:async()=>({})};if(opt.method==='POST'&&holdPost)await holdPost;
 const d=p==='/api/status'?{...data,...(opt.headers?.['X-Shunya-Key']==='1234ABCD'?{system:{ota_url:'https://example.com',ota_min:10,remote_sec:60}}:{})}:p==='/api/events'?{events:[]}:p.startsWith('/api/history')?{samples:[]}:{message:'Saved'};
 return {status:200,ok:true,json:async()=>d}}
const stored=new Map();const context={console,document,fetch,navigator:{},localStorage:{getItem:k=>stored.get(k),setItem:(k,v)=>stored.set(k,v),removeItem:k=>stored.delete(k)},window:{},location:{hash:''},AbortSignal:{timeout(){}},Date:class extends Date{static now(){return clock}},setTimeout(fn,ms){const id=++seq;timers.set(id,{fn,at:clock+ms});return id},clearTimeout:id=>timers.delete(id),setInterval(fn,ms){intervals.push([fn,ms])},confirm(){return false},prompt(){return null}};
vm.createContext(context);vm.runInContext(script,context);const run=s=>vm.runInContext(s,context);const flush=async()=>{for(let i=0;i<15;i++)await Promise.resolve()};
async function advance(ms){clock+=ms;for(const [id,t] of [...timers])if(t.at<=clock){timers.delete(id);t.fn()}await flush()}
(async()=>{await flush();assert.equal($('name').textContent,'Ujjain');assert.equal(intervals[0][1],2000);assert.equal($('authPrompt').hidden,true);assert(!calls[0][1].headers['X-Shunya-Key']);assert.equal($('why-mist').textContent,'next mist in 1:12:30');
 assert.equal($('pill-heater').querySelectorAll('button').length,2);assert(!$('pill-heater').querySelectorAll('button').some(b=>b.dataset.action==='ON'));assert(!source.includes('grip')&&!source.includes('/api/order'));
 // Climate writes need no code: sent immediately without a key and without a prompt.
 const first=run("post('/api/target',{temperature_target:28.5})");await first;assert($('authPrompt').hidden);const p1=calls.find(c=>c[1].method==='POST');assert.equal(JSON.parse(p1[1].body).temperature_target,28.5);assert(!p1[1].headers['X-Shunya-Key']);
 // Optimistic sliding pill must move before the POST resolves.
 let release;holdPost=new Promise(r=>release=r);const off=$('pill-heater').querySelectorAll('button').find(b=>b.dataset.action==='OFF');const clicked=off.onclick();assert.equal($('pill-heater').dataset.m,'off');assert.equal(off.getAttribute('aria-pressed'),'true');release();holdPost=null;await clicked;clock+=400;
 // 401 prompts inline and is retried after unlock, with the same existing endpoint.
 unauthorized=true;const rejected=run("post('/api/device/mist/MIST',{})");await flush();assert(!$('authPrompt').hidden);assert($('authMessage').textContent.includes('not accepted'));unauthorized=false;$('key').value='1234abcd';await $('authForm').onsubmit({preventDefault(){}});await rejected;assert.equal(stored.get('shunya-key'),'1234ABCD');
 // Batch different advanced edits inside one 500-ms debounce, no dropped field.
 calls.length=0;$('cfg-top_t_offset').value='1.5';$('cfg-top_t_offset').events.input();$('cfg-bottom_rh_offset').value='-4';$('cfg-bottom_rh_offset').events.input();$('humidityReducer').value='wettest';$('humidityReducer').onchange();await advance(499);assert(!calls.some(c=>c[1].method==='POST'));await advance(1);
 const saved=calls.find(c=>c[0]==='/api/config');assert.deepEqual(JSON.parse(saved[1].body),{top_t_offset:1.5,bottom_rh_offset:-4,humidity_reducer:'wettest'});assert($('sv').classList.contains('show'));
 // Polling must preserve focused input and dragging dial.
 $('cfg-top_t_offset').focus();$('cfg-top_t_offset').value='2.2';run('render(state)');assert.equal($('cfg-top_t_offset').value,'2.2');
 const svg=$('dt').querySelector('svg');svg.events.pointerdown({preventDefault(){},pointerId:1,clientX:240,clientY:120});const dragged=$('dt').getAttribute('aria-valuenow');run('render(state)');assert.equal($('dt').getAttribute('aria-valuenow'),dragged);svg.events.pointerup({pointerId:1});await flush();assert(calls.some(c=>c[0]==='/api/target'&&JSON.parse(c[1].body).temperature_target>=20));
 run("render({...state,top:{valid:false},banner_message:{text:'SENSOR DEGRADED',kind:'advisory'},blocked_by:{heater:'sensor degraded',humidifier:'post-heat lockout',mist:'post-heat lockout'},water_lock_remaining_sec:102,mist_pending:true})");assert($('sen-top').classList.contains('bad'));assert.equal($('alerts').textContent,'SENSOR DEGRADED');assert($('why-humidifier').textContent.includes('1:42'));assert($('why-mist').textContent.includes('pending'));
 run("render({...state,banner_message:{text:'',kind:''}})");assert($('alerts').hidden);
 for(const id of ['history','eventsSection','wifi','system','help']){run(`tab('${id}')`);assert($(id).classList.contains('visible'))}await flush();
 console.log('PASS: 2-s public polling, two SVG dials, immediate mode pills, inline auth/401 retry, JSON writes, 500-ms batched Advanced/SAVED, focus/drag preservation, reasons/countdowns, secondary navigation');
})().catch(e=>{console.error(e);process.exitCode=1});
