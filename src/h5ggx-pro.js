(()=>{
const h5=()=>typeof window.h5gg==='object';
const call=(n,...a)=>h5()&&typeof h5gg[n]==='function'?h5gg[n](...a):null;
const $=s=>document.querySelector(s);
const esc=s=>String(s).replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
const hex=(n,w=2)=>'0x'+BigInt(n).toString(16).toUpperCase().padStart(w,'0');
const toBig=v=>{try{return BigInt(String(v).trim())}catch{return null}};
const patches=JSON.parse(localStorage.getItem('h5ggx.pro.patches')||'[]');
let ranges=[];
function save(){localStorage.setItem('h5ggx.pro.patches',JSON.stringify(patches))}
function base(p){return String(p||'').split('/').filter(Boolean).pop()||'module'}
function getRanges(){try{ranges=call('getRangesList')||[]}catch{ranges=[]}return ranges}
function moduleOffset(addr){const a=toBig(addr);if(a===null)return String(addr);if(!ranges.length)getRanges();for(const m of ranges){const s=toBig(m.start),e=toBig(m.end);if(s!==null&&e!==null&&a>=s&&a<e)return `${base(m.name)}+0x${(a-s).toString(16).toUpperCase()}`}return '0x'+a.toString(16).toUpperCase()}
function resolve(text){const raw=String(text||'').trim();if(!raw)return null;const m=raw.match(/^(.+?)\+\s*(0x[0-9a-fA-F]+|\d+)$/);if(m){if(!ranges.length)getRanges();const r=ranges.find(x=>base(x.name)===m[1].trim()||x.name===m[1].trim());const s=r&&toBig(r.start),o=toBig(m[2]);return s!==null&&s!==false&&o!==null?s+o:null}return toBig(raw)}
function readU8(a){try{return Number(call('getValue','0x'+a.toString(16),'U8'))&255}catch{return 0}}
function readU32(a){try{return Number(call('getValue','0x'+a.toString(16),'U32'))>>>0}catch{return 0}}
function writeU8(a,v){return call('setValue','0x'+a.toString(16),String(v&255),'U8')}
function writeU32(a,v){return call('setValue','0x'+a.toString(16),String(v>>>0),'U32')}
function sx(v,bits){const s=1<<(bits-1);return (v&s)?v-(1<<bits):v}
function decode(w,a){w>>>=0;
 if(w===0xD503201F)return {asm:'NOP'};
 if(w===0xD65F03C0)return {asm:'RET'};
 if((w&0xFFFFFC1F)===0xD61F0000)return {asm:`BR X${(w>>>5)&31}`};
 if((w&0xFFFFFC1F)===0xD63F0000)return {asm:`BLR X${(w>>>5)&31}`};
 if((w&0xFC000000)===0x14000000||(w&0xFC000000)===0x94000000){let imm=w&0x03FFFFFF;if(imm&0x02000000)imm-=0x04000000;const t=a+BigInt(imm*4);return {asm:`${(w&0xFC000000)===0x94000000?'BL':'B'} ${moduleOffset(t)}`,target:t}}
 if((w&0x7E000000)===0x34000000){let imm=(w>>>5)&0x7FFFF;if(imm&0x40000)imm-=0x80000;const t=a+BigInt(imm*4),rt=w&31;return {asm:`${(w&0x01000000)?'CBNZ':'CBZ'} ${w>>>31?'X':'W'}${rt}, ${moduleOffset(t)}`,target:t}}
 if((w&0x7E000000)===0x36000000){let imm=(w>>>5)&0x3FFF;if(imm&0x2000)imm-=0x4000;const t=a+BigInt(imm*4),rt=w&31,bit=((w>>>19)&0x20)|((w>>>31)&1);return {asm:`${(w&0x01000000)?'TBNZ':'TBZ'} ${w>>>31?'X':'W'}${rt}, #${bit}, ${moduleOffset(t)}`,target:t}}
 if((w&0xFF000010)===0x54000000){let imm=(w>>>5)&0x7FFFF;if(imm&0x40000)imm-=0x80000;const t=a+BigInt(imm*4);return {asm:`B.cond #${w&15}, ${moduleOffset(t)}`,target:t}}
 if((w&0x9F000000)===0x90000000)return {asm:`ADRP X${w&31}, ...`};
 if((w&0x7F000000)===0x11000000)return {asm:`ADD/SUB imm`};
 if((w&0x3B000000)===0x39000000)return {asm:`LDR/STR`};
 return {asm:`.word 0x${w.toString(16).toUpperCase().padStart(8,'0')}`}
}
function addStyle(){const s=document.createElement('style');s.textContent=`
.proBox{margin-top:12px;padding:12px;border:1px solid #2c3953;border-radius:14px;background:#0b1320}.proTitle{font-weight:800;margin-bottom:8px}.proGrid{display:grid;grid-template-columns:1fr auto auto;gap:7px}.hexDump{font:12px ui-monospace,SFMono-Regular,Menlo,monospace;overflow:auto;border:1px solid #27334a;border-radius:10px}.hexRow{display:grid;grid-template-columns:135px 1fr 130px;gap:8px;padding:7px;border-bottom:1px solid #202a3c}.hexBytes{white-space:nowrap}.hexByte{display:inline-block;padding:2px 3px;border-radius:4px;cursor:pointer}.hexByte:hover{background:#253451}.hexAscii{color:#91a1bc;white-space:pre}.proAsm{font:12px ui-monospace,SFMono-Regular,Menlo,monospace}.proInst{display:grid;grid-template-columns:145px 90px 1fr auto;gap:8px;align-items:center;padding:7px;border-bottom:1px solid #253047}.proBtns{display:flex;gap:5px}.proBtns button{min-height:30px;padding:4px 7px;font-size:11px}.patchRow{display:grid;grid-template-columns:1fr auto;gap:8px;padding:7px;border-bottom:1px solid #253047}.muted{color:#8390a7;font-size:11px}@media(max-width:650px){.proGrid{grid-template-columns:1fr}.proInst{grid-template-columns:1fr 90px}.proInst .proAsm{grid-column:1/-1}.proBtns{grid-column:1/-1}.hexRow{grid-template-columns:1fr}.hexAscii{display:none}}`;
document.head.appendChild(s)}
function mount(){const host=$('#arm64');if(!host||$('#h5ggxPro'))return;addStyle();const wrap=document.createElement('div');wrap.id='h5ggxPro';wrap.innerHTML=`
<div class="proBox"><div class="proTitle">ARM64 Pro Disassembly</div><div class="proGrid"><input id="proAddr" placeholder="address / module+offset"><input id="proCount" type="number" value="32" min="1" max="256"><button id="proRead">解析</button></div><div id="proAsm" class="proAsm"></div></div>
<div class="proBox"><div class="proTitle">Hex Viewer</div><div class="proGrid"><input id="hexAddr" placeholder="address / module+offset"><input id="hexLen" type="number" value="128" min="16" max="2048"><button id="hexRead">表示</button></div><div id="hexDump" class="hexDump"></div><div class="muted">バイトをタップするとランタイムU8編集</div></div>
<div class="proBox"><div class="proTitle">Runtime Patch List</div><div id="proPatchList"></div></div>`;host.appendChild(wrap);$('#proRead').onclick=renderDisasm;$('#hexRead').onclick=renderHex;renderPatchList()}
function rememberPatch(addr,original,current,type){const key='0x'+addr.toString(16).toUpperCase();let p=patches.find(x=>x.address===key&&x.type===type);if(!p){p={address:key,original,current,type,when:Date.now()};patches.unshift(p)}else p.current=current;save();renderPatchList()}
function nop(addr){const old=readU32(addr);if(writeU32(addr,0xD503201F)!==false){rememberPatch(addr,old,0xD503201F,'U32');renderDisasm()}}
function patchWord(addr){const old=readU32(addr),v=prompt('新しいARM64 U32 HEX',old.toString(16).toUpperCase().padStart(8,'0'));if(v===null||!/^[0-9a-fA-F]{8}$/.test(v))return;const n=parseInt(v,16)>>>0;if(writeU32(addr,n)!==false){rememberPatch(addr,old,n,'U32');renderDisasm()}}
function follow(addr){$('#proAddr').value='0x'+addr.toString(16).toUpperCase();$('#armAddress').value=$('#proAddr').value;renderDisasm()}
function renderDisasm(){getRanges();const start=resolve($('#proAddr').value||$('#armAddress').value);if(start===null)return alert('アドレス形式エラー');const n=Math.max(1,Math.min(256,Number($('#proCount').value)||32));let out='';for(let i=0;i<n;i++){const a=(start&~3n)+BigInt(i*4),w=readU32(a),d=decode(w,a);const av='0x'+a.toString(16).toUpperCase();out+=`<div class="proInst"><div><div>${esc(moduleOffset(a))}</div><div class="muted">${av}</div></div><div>0x${w.toString(16).toUpperCase().padStart(8,'0')}</div><div class="proAsm">${esc(d.asm)}</div><div class="proBtns"><button data-pro-nop="${av}">NOP</button><button data-pro-edit="${av}">編集</button>${d.target!==undefined?`<button data-pro-follow="0x${d.target.toString(16).toUpperCase()}">追跡</button>`:''}</div></div>`}$('#proAsm').innerHTML=out}
function renderHex(){getRanges();const start=resolve($('#hexAddr').value||$('#armAddress').value);if(start===null)return alert('アドレス形式エラー');const len=Math.max(16,Math.min(2048,Number($('#hexLen').value)||128));let out='';for(let off=0;off<len;off+=16){let bytes=[],ascii='';for(let i=0;i<16&&off+i<len;i++){const a=start+BigInt(off+i),b=readU8(a);bytes.push(`<span class="hexByte" data-byte-addr="0x${a.toString(16).toUpperCase()}" data-byte-val="${b}">${b.toString(16).toUpperCase().padStart(2,'0')}</span>`);ascii+=(b>=32&&b<127)?String.fromCharCode(b):'.'}const a=start+BigInt(off);out+=`<div class="hexRow"><div>${esc(moduleOffset(a))}</div><div class="hexBytes">${bytes.join(' ')}</div><div class="hexAscii">${esc(ascii)}</div></div>`}$('#hexDump').innerHTML=out}
function editByte(addr,old){const v=prompt('新しいU8 (HEX 00-FF)',Number(old).toString(16).toUpperCase().padStart(2,'0'));if(v===null||!/^[0-9a-fA-F]{1,2}$/.test(v))return;const n=parseInt(v,16);if(writeU8(addr,n)!==false){rememberPatch(addr,Number(old),n,'U8');renderHex()}}
function undo(i){const p=patches[i];if(!p)return;const a=toBig(p.address);if(a===null)return;const ok=p.type==='U8'?writeU8(a,p.original):writeU32(a,p.original);if(ok!==false){patches.splice(i,1);save();renderPatchList();if($('#proAddr')&&$('#proAddr').value)renderDisasm();if($('#hexAddr')&&$('#hexAddr').value)renderHex()}}
function renderPatchList(){const el=$('#proPatchList');if(!el)return;el.innerHTML=patches.length?patches.map((p,i)=>`<div class="patchRow"><div><b>${esc(moduleOffset(p.address))}</b><div class="muted">${esc(p.address)} · ${esc(p.type)} · ${p.type==='U32'?'0x'+Number(p.original).toString(16).toUpperCase().padStart(8,'0'):'0x'+Number(p.original).toString(16).toUpperCase().padStart(2,'0')} → ${p.type==='U32'?'0x'+Number(p.current).toString(16).toUpperCase().padStart(8,'0'):'0x'+Number(p.current).toString(16).toUpperCase().padStart(2,'0')}</div></div><button data-pro-undo="${i}">復元</button></div>`).join(''):'<div class="empty">パッチなし</div>'}
document.addEventListener('click',e=>{const t=e.target;if(t.dataset.proNop)nop(toBig(t.dataset.proNop));if(t.dataset.proEdit)patchWord(toBig(t.dataset.proEdit));if(t.dataset.proFollow)follow(toBig(t.dataset.proFollow));if(t.dataset.byteAddr)editByte(toBig(t.dataset.byteAddr),t.dataset.byteVal);if(t.dataset.proUndo!==undefined)undo(Number(t.dataset.proUndo))});
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',mount);else mount();
})();
