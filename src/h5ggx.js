(()=>{
const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
const hasH5=()=>typeof window.h5gg==='object';
const state={
  results:[], snapshot:new Map(), modules:[], armRows:[],
  bookmarks:JSON.parse(localStorage.getItem('h5ggx.bookmarks')||'[]'),
  history:JSON.parse(localStorage.getItem('h5ggx.history')||'[]'),
  watch:JSON.parse(localStorage.getItem('h5ggx.watch')||'[]'),
  patches:JSON.parse(localStorage.getItem('h5ggx.patches')||'[]')
};
const save=()=>{localStorage.setItem('h5ggx.bookmarks',JSON.stringify(state.bookmarks));localStorage.setItem('h5ggx.history',JSON.stringify(state.history));localStorage.setItem('h5ggx.watch',JSON.stringify(state.watch));localStorage.setItem('h5ggx.patches',JSON.stringify(state.patches));};
const esc=s=>String(s).replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
const hx=(n,w=8)=>'0x'+(BigInt(n) & ((1n<<BigInt(w*4))-1n)).toString(16).padStart(w,'0').toUpperCase();
const toBig=v=>{try{return BigInt(String(v).trim())}catch{return null}};
$('#mode').textContent=hasH5()?'H5GG LIVE':'DEMO';

function call(name,...args){if(hasH5()&&typeof h5gg[name]==='function')return h5gg[name](...args);return null}
function demoResults(){return Array.from({length:18},(_,i)=>({address:'0x'+(0x100000000+i*0x40).toString(16),value:String((i+1)*10),type:$('#type').value}))}

async function loadResults(){
 let r=[];
 if(hasH5()){
   try{const count=Number(call('getResultsCount')||0);r=call('getResults',Math.min(count||100,500),0)||[]}catch(e){console.warn(e)}
 }else r=demoResults();
 state.results=r.map(x=>({address:x.address||x.addr||'',value:String(x.value??''),type:x.type||$('#type').value,selected:false}));
 renderResults();
}
async function search(){
 const value=$('#value').value.trim(),type=$('#type').value,start=$('#rangeStart').value.trim(),end=$('#rangeEnd').value.trim();if(!value)return;
 try{call('clearResults');call('searchNumber',value,type,start,end);state.history.unshift({value,type,start,end,ts:Date.now()});state.history=state.history.slice(0,50);save();renderHistory();await loadResults()}catch(e){alert('検索エラー: '+e.message)}
}
function nearby(){const value=$('#value').value.trim(),type=$('#type').value;if(!value)return;try{call('searchNearby',value,type,'0x100');loadResults()}catch(e){alert('Nearbyエラー: '+e.message)}}
function diffMatch(item,mode){if(mode==='all')return true;const old=state.snapshot.get(item.address);if(old===undefined)return false;const a=Number(old),b=Number(item.value);if(mode==='changed')return String(old)!==String(item.value);if(mode==='unchanged')return String(old)===String(item.value);if(mode==='increased')return Number.isFinite(a)&&Number.isFinite(b)&&b>a;if(mode==='decreased')return Number.isFinite(a)&&Number.isFinite(b)&&b<a;return true}
function renderResults(){const q=$('#filter').value.toLowerCase(),mode=$('#diffMode').value;const items=state.results.filter(x=>(x.address+' '+x.value).toLowerCase().includes(q)&&diffMatch(x,mode));$('#resultList').innerHTML=items.length?items.map(x=>`<div class="item"><input type="checkbox" data-select="${esc(x.address)}" ${x.selected?'checked':''}><div class="meta"><div class="val">${esc(x.value)}</div><div class="addr">${esc(x.address)}</div><div class="sub">${esc(x.type)}</div></div><div class="itemActions"><button class="mini" data-watch="${esc(x.address)}">監視</button><button class="mini" data-bookmark="${esc(x.address)}">保存</button></div></div>`).join(''):'<div class="empty">結果なし</div>'}
function renderBookmarks(){$('#bookmarkList').innerHTML=state.bookmarks.length?state.bookmarks.map(x=>`<div class="item"><span>★</span><div class="meta"><div class="val">${esc(x.label||x.value)}</div><div class="addr">${esc(x.address)}</div><div class="sub">${esc(x.type)}</div></div><div class="itemActions"><button class="mini" data-writebm="${esc(x.address)}">編集</button><button class="mini" data-delbm="${esc(x.address)}">削除</button></div></div>`).join(''):'<div class="empty">保存なし</div>'}
function renderHistory(){$('#historyList').innerHTML=state.history.length?state.history.map((x,i)=>`<div class="item"><span>↻</span><div class="meta"><div class="val">${esc(x.value)}</div><div class="sub">${esc(x.type)} ・ ${new Date(x.ts).toLocaleString()}</div></div><div class="itemActions"><button class="mini" data-history="${i}">再検索</button></div></div>`).join(''):'<div class="empty">履歴なし</div>'}
async function readValue(address,type){try{if(hasH5())return String(call('getValue',address,type));const f=state.results.find(x=>x.address===address);return f?f.value:'?'}catch{return '?'}}
async function renderWatch(){const rows=[];for(const x of state.watch)rows.push({...x,current:await readValue(x.address,x.type)});$('#watchList').innerHTML=rows.length?rows.map(x=>`<div class="item"><span>●</span><div class="meta"><div class="val">${esc(x.current)}</div><div class="addr">${esc(x.address)}</div><div class="sub">${esc(x.type)}</div></div><div class="itemActions"><button class="mini" data-delwatch="${esc(x.address)}">解除</button></div></div>`).join(''):'<div class="empty">監視なし</div>'}
function addBookmark(address){const x=state.results.find(r=>r.address===address);if(!x)return;if(!state.bookmarks.some(b=>b.address===address))state.bookmarks.unshift({address:x.address,value:x.value,type:x.type,label:''});save();renderBookmarks()}
function addWatch(address){const x=state.results.find(r=>r.address===address);if(!x)return;if(!state.watch.some(b=>b.address===address))state.watch.unshift({address:x.address,type:x.type});save();renderWatch()}
async function refreshValues(){for(const x of state.results)x.value=await readValue(x.address,x.type);renderResults();renderWatch()}
function snapshot(){state.snapshot=new Map(state.results.map(x=>[x.address,x.value]))}
function bulkSet(){const val=$('#bulkValue').value;if(val==='')return;for(const x of state.results.filter(x=>x.selected)){try{call('setValue',x.address,val,x.type);x.value=val}catch(e){console.warn(e)}}renderResults()}

function basename(p){return String(p||'').split('/').filter(Boolean).pop()||String(p||'module')}
function loadModules(){
 let mods=[];try{mods=hasH5()?(call('getRangesList')||[]):[]}catch(e){console.warn(e)}
 if(!mods.length&&!hasH5())mods=[{name:'/Demo/AppDemo',start:'0x100000000',end:'0x100020000'},{name:'/Demo/Frameworks/GameCore.framework/GameCore',start:'0x180000000',end:'0x180010000'}];
 state.modules=mods.map((m,i)=>({name:m.name||('module'+i),start:String(m.start),end:String(m.end)}));
 $('#moduleSelect').innerHTML=state.modules.map((m,i)=>`<option value="${i}">${esc(basename(m.name))}  ${esc(m.start)}</option>`).join('');
 showModuleInfo();
}
function selectedModule(){return state.modules[Number($('#moduleSelect').value)||0]||null}
function showModuleInfo(){const m=selectedModule();if(!m){$('#moduleInfo').textContent='モジュールなし';return}const s=toBig(m.start),e=toBig(m.end);const size=s!==null&&e!==null?hx(e-s,1):'?';$('#moduleInfo').textContent=`${m.name}\nBASE ${m.start}   END ${m.end}   SIZE ${size}`}
function resolveArmAddress(text){
 const raw=String(text||'').trim();if(!raw)return null;
 const plus=raw.match(/^(.+?)\+\s*(0x[0-9a-fA-F]+|\d+)$/);if(plus){const key=plus[1].trim();const m=state.modules.find(x=>basename(x.name)===key||x.name===key)||selectedModule();const b=m?toBig(m.start):null,o=toBig(plus[2]);return b!==null&&o!==null?b+o:null}
 return toBig(raw)
}
function decodeArm64(word,address){
 const w=Number(word>>>0);
 if(w===0xD503201F)return 'NOP';if(w===0xD65F03C0)return 'RET';if((w&0xFFE0001F)===0xD4200000)return `BRK #${(w>>>5)&0xFFFF}`;
 if((w&0xFC000000)===0x14000000||(w&0xFC000000)===0x94000000){let imm=w&0x03FFFFFF;if(imm&0x02000000)imm-=0x04000000;const target=BigInt(address)+BigInt(imm*4);return `${(w&0xFC000000)===0x94000000?'BL':'B'} ${'0x'+target.toString(16).toUpperCase()}`}
 if((w&0xFFFFFC1F)===0xD61F0000)return `BR X${(w>>>5)&31}`;if((w&0xFFFFFC1F)===0xD63F0000)return `BLR X${(w>>>5)&31}`;
 return 'ARM64  '+hx(w,8)
}
function readU32(addr){if(hasH5()){const v=call('getValue','0x'+addr.toString(16),'U32');const n=Number(v);return Number.isFinite(n)?(n>>>0):0}const demo=[0xD503201F,0x94000002,0xD503201F,0xD65F03C0];return demo[Number((addr>>2n)%4n)]>>>0}
function renderArmRows(){
 $('#armList').innerHTML=state.armRows.length?state.armRows.map(r=>{const patched=state.patches.some(p=>p.address===r.address);return `<div class="armRow ${patched?'changedRuntime':''}"><div class="armAddr">${esc(r.address)}</div><div class="armHex">${esc(r.hex)}</div><div class="armAsm">${esc(r.asm)}</div><div class="armPatch"><input data-patchinput="${esc(r.address)}" value="${esc(r.hex.replace('0x',''))}" maxlength="8"><button data-patch="${esc(r.address)}">適用</button>${patched?`<button data-restore="${esc(r.address)}">戻す</button>`:''}</div></div>`}).join(''):'<div class="empty">アドレスを指定してARM64を読み込んでください</div>'
}
function readArm64(){
 const start=resolveArmAddress($('#armAddress').value);if(start===null){alert('アドレス形式を確認してください');return}let count=Math.max(1,Math.min(128,Number($('#armCount').value)||24));const aligned=start&~3n;state.armRows=[];
 for(let i=0;i<count;i++){const a=aligned+BigInt(i*4),w=readU32(a);state.armRows.push({address:'0x'+a.toString(16).toUpperCase(),word:w,hex:hx(w,8),asm:decodeArm64(w,a)})}
 renderArmRows()
}
function runtimePatch(address,hex){
 if(!/^[0-9a-fA-F]{8}$/.test(hex)){alert('ARM64命令は8桁の16進数(U32)で入力してください');return}
 const word=parseInt(hex,16)>>>0;const row=state.armRows.find(r=>r.address.toLowerCase()===address.toLowerCase());if(!row)return;
 try{if(hasH5()){const ok=call('setValue',address,String(word),'U32');if(ok===false){alert('書き込みに失敗しました');return}}if(!state.patches.some(p=>p.address===address))state.patches.push({address,original:row.word,type:'U32'});row.word=word;row.hex=hx(word,8);row.asm=decodeArm64(word,toBig(address));save();renderArmRows()}catch(e){alert('ランタイム書き込みエラー: '+e.message)}
}
function restorePatch(address){const p=state.patches.find(p=>p.address===address);if(!p)return;try{if(hasH5())call('setValue',address,String(p.original>>>0),'U32');const row=state.armRows.find(r=>r.address===address);if(row){row.word=p.original>>>0;row.hex=hx(row.word,8);row.asm=decodeArm64(row.word,toBig(address))}state.patches=state.patches.filter(x=>x.address!==address);save();renderArmRows()}catch(e){alert('復元エラー: '+e.message)}}

function exportData(){const blob=new Blob([JSON.stringify({bookmarks:state.bookmarks,watch:state.watch,history:state.history,patches:state.patches},null,2)],{type:'application/json'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='h5ggx-session.json';a.click();URL.revokeObjectURL(a.href)}

$('#searchBtn').onclick=search;$('#nearBtn').onclick=nearby;$('#clearBtn').onclick=()=>{call('clearResults');state.results=[];renderResults()};$('#snapshotBtn').onclick=snapshot;$('#refreshBtn').onclick=refreshValues;$('#bulkSetBtn').onclick=bulkSet;$('#filter').oninput=renderResults;$('#diffMode').onchange=renderResults;$('#exportBtn').onclick=exportData;
$('#reloadModulesBtn').onclick=loadModules;$('#moduleSelect').onchange=showModuleInfo;$('#armReadBtn').onclick=readArm64;$('#armUseModuleBtn').onclick=()=>{const m=selectedModule();if(m)$('#armAddress').value=m.start};
$$('.tabs button').forEach(b=>b.onclick=()=>{$$('.tabs button').forEach(x=>x.classList.remove('active'));$$('.panel').forEach(x=>x.classList.remove('active'));b.classList.add('active');$('#'+b.dataset.tab).classList.add('active');if(b.dataset.tab==='watch')renderWatch();if(b.dataset.tab==='arm64'&&!state.modules.length)loadModules()});
document.addEventListener('change',e=>{if(e.target.dataset.select){const x=state.results.find(r=>r.address===e.target.dataset.select);if(x)x.selected=e.target.checked}});
document.addEventListener('click',e=>{const t=e.target;if(t.dataset.bookmark)addBookmark(t.dataset.bookmark);if(t.dataset.watch)addWatch(t.dataset.watch);if(t.dataset.delbm){state.bookmarks=state.bookmarks.filter(x=>x.address!==t.dataset.delbm);save();renderBookmarks()}if(t.dataset.delwatch){state.watch=state.watch.filter(x=>x.address!==t.dataset.delwatch);save();renderWatch()}if(t.dataset.history!==undefined){const x=state.history[Number(t.dataset.history)];if(x){$('#value').value=x.value;$('#type').value=x.type;$('#rangeStart').value=x.start;$('#rangeEnd').value=x.end;search()}}if(t.dataset.writebm){const x=state.bookmarks.find(b=>b.address===t.dataset.writebm);if(x){const v=prompt('新しい値',x.value);if(v!==null){call('setValue',x.address,v,x.type);x.value=v;save();renderBookmarks()}}}if(t.dataset.patch){const inp=document.querySelector(`[data-patchinput="${CSS.escape(t.dataset.patch)}"]`);if(inp)runtimePatch(t.dataset.patch,inp.value.trim())}if(t.dataset.restore)restorePatch(t.dataset.restore)});
renderResults();renderBookmarks();renderHistory();renderWatch();renderArmRows();setInterval(()=>{if(state.watch.length)renderWatch()},1000);
})();
