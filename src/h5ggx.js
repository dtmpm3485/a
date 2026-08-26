(()=>{
const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
const hasH5=()=>typeof window.h5gg==='object';
const state={results:[],snapshot:new Map(),bookmarks:JSON.parse(localStorage.getItem('h5ggx.bookmarks')||'[]'),history:JSON.parse(localStorage.getItem('h5ggx.history')||'[]'),watch:JSON.parse(localStorage.getItem('h5ggx.watch')||'[]')};
const save=()=>{localStorage.setItem('h5ggx.bookmarks',JSON.stringify(state.bookmarks));localStorage.setItem('h5ggx.history',JSON.stringify(state.history));localStorage.setItem('h5ggx.watch',JSON.stringify(state.watch));};
const esc=s=>String(s).replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
$('#mode').textContent=hasH5()?'H5GG':'Demo';

function call(name,...args){if(hasH5()&&typeof h5gg[name]==='function')return h5gg[name](...args);return null}
function demoResults(){return Array.from({length:18},(_,i)=>({address:'0x'+(0x100000000+i*0x40).toString(16),value:String((i+1)*10),type:$('#type').value}))}

async function loadResults(){
 let r=[];
 if(hasH5()){
   try{const count=call('getResultsCount')||100; r=call('getResults',Math.min(Number(count)||100,500))||[]}catch(e){console.warn(e)}
 } else r=demoResults();
 state.results=r.map(x=>({address:x.address||x.addr||'',value:String(x.value??''),type:x.type||$('#type').value,selected:false}));
 renderResults();
}

async function search(){
 const value=$('#value').value.trim(),type=$('#type').value,start=$('#rangeStart').value.trim(),end=$('#rangeEnd').value.trim();
 if(!value)return;
 try{call('clearResults');call('searchNumber',value,type,start,end);state.history.unshift({value,type,start,end,ts:Date.now()});state.history=state.history.slice(0,50);save();renderHistory();await loadResults();}catch(e){alert('検索エラー: '+e.message)}
}
function nearby(){
 const value=$('#value').value.trim(),type=$('#type').value;if(!value)return;
 try{if(hasH5()&&typeof h5gg.searchNearby==='function')h5gg.searchNearby(value,type,'0x100');loadResults();}catch(e){alert('Nearbyエラー: '+e.message)}
}
function diffMatch(item,mode){if(mode==='all')return true;const old=state.snapshot.get(item.address);if(old===undefined)return false;const a=Number(old),b=Number(item.value);if(mode==='changed')return String(old)!==String(item.value);if(mode==='unchanged')return String(old)===String(item.value);if(mode==='increased')return !Number.isNaN(a)&&!Number.isNaN(b)&&b>a;if(mode==='decreased')return !Number.isNaN(a)&&!Number.isNaN(b)&&b<a;return true}
function renderResults(){
 const q=$('#filter').value.toLowerCase(),mode=$('#diffMode').value;
 const items=state.results.filter(x=>(x.address+' '+x.value).toLowerCase().includes(q)&&diffMatch(x,mode));
 $('#resultList').innerHTML=items.length?items.map((x,i)=>`<div class="item"><input type="checkbox" data-select="${esc(x.address)}" ${x.selected?'checked':''}><div class="meta"><div class="val">${esc(x.value)}</div><div class="addr">${esc(x.address)}</div><div class="sub">${esc(x.type)}</div></div><div class="itemActions"><button class="mini" data-watch="${esc(x.address)}">監視</button><button class="mini" data-bookmark="${esc(x.address)}">保存</button></div></div>`).join(''):'<div class="empty">結果なし</div>';
}
function renderBookmarks(){
 $('#bookmarkList').innerHTML=state.bookmarks.length?state.bookmarks.map(x=>`<div class="item"><span>★</span><div class="meta"><div class="val">${esc(x.label||x.value)}</div><div class="addr">${esc(x.address)}</div><div class="sub">${esc(x.type)}</div></div><div class="itemActions"><button class="mini" data-writebm="${esc(x.address)}">編集</button><button class="mini" data-delbm="${esc(x.address)}">削除</button></div></div>`).join(''):'<div class="empty">保存なし</div>'
}
function renderHistory(){
 $('#historyList').innerHTML=state.history.length?state.history.map((x,i)=>`<div class="item"><span>↻</span><div class="meta"><div class="val">${esc(x.value)}</div><div class="sub">${esc(x.type)} ・ ${new Date(x.ts).toLocaleString()}</div></div><div class="itemActions"><button class="mini" data-history="${i}">再検索</button></div></div>`).join(''):'<div class="empty">履歴なし</div>'
}
async function readValue(address,type){try{if(hasH5())return String(call('getValue',address,type));const f=state.results.find(x=>x.address===address);return f?f.value:'?'}catch{return '?'}}
async function renderWatch(){
 const rows=[];for(const x of state.watch){rows.push({...x,current:await readValue(x.address,x.type)})}
 $('#watchList').innerHTML=rows.length?rows.map(x=>`<div class="item"><span>●</span><div class="meta"><div class="val">${esc(x.current)}</div><div class="addr">${esc(x.address)}</div><div class="sub">${esc(x.type)}</div></div><div class="itemActions"><button class="mini" data-delwatch="${esc(x.address)}">解除</button></div></div>`).join(''):'<div class="empty">監視なし</div>'
}
function addBookmark(address){const x=state.results.find(r=>r.address===address);if(!x)return;if(!state.bookmarks.some(b=>b.address===address))state.bookmarks.unshift({address:x.address,value:x.value,type:x.type,label:''});save();renderBookmarks()}
function addWatch(address){const x=state.results.find(r=>r.address===address);if(!x)return;if(!state.watch.some(b=>b.address===address))state.watch.unshift({address:x.address,type:x.type});save();renderWatch()}
async function refreshValues(){for(const x of state.results)x.value=await readValue(x.address,x.type);renderResults();renderWatch()}
function snapshot(){state.snapshot=new Map(state.results.map(x=>[x.address,x.value]));}
function bulkSet(){const val=$('#bulkValue').value;if(val==='')return;const selected=state.results.filter(x=>x.selected);for(const x of selected){try{call('setValue',x.address,val,x.type);x.value=val}catch(e){console.warn(e)}}renderResults()}
function exportData(){const blob=new Blob([JSON.stringify({bookmarks:state.bookmarks,watch:state.watch,history:state.history},null,2)],{type:'application/json'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='h5ggx-session.json';a.click();URL.revokeObjectURL(a.href)}

$('#searchBtn').onclick=search;$('#nearBtn').onclick=nearby;$('#clearBtn').onclick=()=>{call('clearResults');state.results=[];renderResults()};$('#snapshotBtn').onclick=snapshot;$('#refreshBtn').onclick=refreshValues;$('#bulkSetBtn').onclick=bulkSet;$('#filter').oninput=renderResults;$('#diffMode').onchange=renderResults;$('#exportBtn').onclick=exportData;
$$('.tabs button').forEach(b=>b.onclick=()=>{$$('.tabs button').forEach(x=>x.classList.remove('active'));$$('.panel').forEach(x=>x.classList.remove('active'));b.classList.add('active');$('#'+b.dataset.tab).classList.add('active');if(b.dataset.tab==='watch')renderWatch()});
document.addEventListener('change',e=>{if(e.target.dataset.select){const x=state.results.find(r=>r.address===e.target.dataset.select);if(x)x.selected=e.target.checked}});
document.addEventListener('click',e=>{const t=e.target;if(t.dataset.bookmark)addBookmark(t.dataset.bookmark);if(t.dataset.watch)addWatch(t.dataset.watch);if(t.dataset.delbm){state.bookmarks=state.bookmarks.filter(x=>x.address!==t.dataset.delbm);save();renderBookmarks()}if(t.dataset.delwatch){state.watch=state.watch.filter(x=>x.address!==t.dataset.delwatch);save();renderWatch()}if(t.dataset.history!==undefined){const x=state.history[Number(t.dataset.history)];if(x){$('#value').value=x.value;$('#type').value=x.type;$('#rangeStart').value=x.start;$('#rangeEnd').value=x.end;search()}}if(t.dataset.writebm){const x=state.bookmarks.find(b=>b.address===t.dataset.writebm);if(x){const v=prompt('新しい値',x.value);if(v!==null){call('setValue',x.address,v,x.type);x.value=v;save();renderBookmarks()}}}});
renderResults();renderBookmarks();renderHistory();renderWatch();setInterval(()=>{if(state.watch.length)renderWatch()},1000);
})();
