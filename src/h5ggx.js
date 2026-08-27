var H5X=(function(){
var S={results:[],snapshot:{},modules:[],armRows:[],bookmarks:[],history:[],watch:[],patches:[],armStart:0};

function el(id){return document.getElementById(id)}
function hasH5(){return typeof h5gg!="undefined" && h5gg}
function call(name){
  var args=[],i;
  for(i=1;i<arguments.length;i++)args.push(arguments[i]);
  if(hasH5() && typeof h5gg[name]=="function") return h5gg[name].apply(h5gg,args);
  return null;
}
function esc(v){return String(v).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;")}
function toNum(v){v=String(v==null?"":v).replace(/^\s+|\s+$/g,"");if(!v)return NaN;return /^0x/i.test(v)?parseInt(v,16):Number(v)}
function addrHex(n){return "0x"+Math.floor(n).toString(16).toUpperCase()}
function hex8(n){var s=(n>>>0).toString(16).toUpperCase();while(s.length<8)s="0"+s;return "0x"+s}
function hex2(n){var s=(n&255).toString(16).toUpperCase();if(s.length<2)s="0"+s;return s}
function basename(p){var a=String(p||"").split("/"),i;for(i=a.length-1;i>=0;i--)if(a[i])return a[i];return "module"}
function arrFind(arr,fn){var i;for(i=0;i<arr.length;i++)if(fn(arr[i],i))return arr[i];return null}
function exists(arr,fn){return arrFind(arr,fn)!=null}

function loadStore(){
  var keys=["bookmarks","history","watch","patches"],i,k;
  for(i=0;i<keys.length;i++){k=keys[i];try{S[k]=JSON.parse(localStorage.getItem("h5ggx."+k)||"[]")}catch(e){S[k]=[]}}
}
function saveStore(){
  var keys=["bookmarks","history","watch","patches"],i,k;
  for(i=0;i<keys.length;i++){k=keys[i];try{localStorage.setItem("h5ggx."+k,JSON.stringify(S[k]))}catch(e){}}
}
function setActiveTab(name){
  var names=["results","arm64","watch","bookmarks","history"],i,p,b;
  for(i=0;i<names.length;i++){
    p=el(names[i]);b=el("tab_"+names[i]);
    if(p)p.className=(names[i]==name)?"panel active":"panel";
    if(b)b.className=(names[i]==name)?"active":"";
  }
  if(name=="arm64" && S.modules.length==0)loadModules();
  if(name=="watch")renderWatch();
  var root=el("scrollRoot");if(root)root.scrollTop=0;
}
function initWindow(){
  try{if(typeof setWindowDrag=="function")setWindowDrag(0,0,500,70)}catch(e){}
  try{
    if(typeof setLayoutAction=="function" && typeof setWindowRect=="function"){
      setLayoutAction(function(w,h){
        var ww,hh,x,y;
        if(w<h){ww=Math.min(430,w-8);hh=Math.min(700,h-24)}
        else{ww=Math.min(620,w-16);hh=Math.min(500,h-16)}
        if(ww<300)ww=w;if(hh<260)hh=h;
        x=Math.max(0,(w-ww)/2);y=Math.max(8,(h-hh)/2);
        setWindowRect(x,y,ww,hh);
      });
    }
  }catch(e){}
}
function init(){
  loadStore();
  if(el("mode"))el("mode").innerHTML=hasH5()?"H5GG LIVE":"DEMO";
  initWindow();
  renderResults();renderWatch();renderBookmarks();renderHistory();renderArm();renderPatches();
  setInterval(function(){if(S.watch.length)renderWatch()},1000);
}
function diag(){
  var s="UI JS: OK\n";
  s+="H5GG API: "+(hasH5()?"OK":"見つかりません")+"\n";
  s+="getRangesList: "+(hasH5()&&typeof h5gg.getRangesList=="function"?"OK":"NG")+"\n";
  s+="setValue: "+(hasH5()&&typeof h5gg.setValue=="function"?"OK":"NG")+"\n";
  s+="スクロール: "+(el("scrollRoot")?"OK":"NG");
  alert(s);
}
function tab(name){setActiveTab(name)}

function search(){
  var v=el("value").value,t=el("type").value,st=el("rangeStart").value,en=el("rangeEnd").value;
  if(!v){alert("検索値を入力してください");return}
  try{
    if(hasH5())call("searchNumber",v,t,st,en);
    S.history.unshift({value:v,type:t,start:st,end:en,ts:(new Date()).getTime()});
    if(S.history.length>50)S.history.length=50;
    saveStore();renderHistory();loadResults();
  }catch(e){alert("検索エラー: "+e)}
}
function nearby(){
  var v=el("value").value,t=el("type").value;
  if(!v){alert("検索値を入力してください");return}
  try{if(hasH5())call("searchNearby",v,t,"0x100");loadResults()}catch(e){alert("Nearbyエラー: "+e)}
}
function clearResults(){try{if(hasH5())call("clearResults")}catch(e){}S.results=[];renderResults()}
function loadResults(){
  var r=[],c=0,i,x;
  try{
    if(hasH5()){c=Number(call("getResultsCount")||0);r=call("getResults",Math.min(c||100,500),0)||[]}
    else{for(i=0;i<12;i++)r.push({address:addrHex(0x100000000+i*64),value:String((i+1)*10),type:el("type").value})}
  }catch(e){r=[]}
  S.results=[];
  for(i=0;i<r.length;i++){x=r[i];S.results.push({address:x.address||x.addr||"",value:String(x.value==null?"":x.value),type:x.type||el("type").value,selected:false})}
  renderResults();
}
function diffOK(x,mode){
  var old,a,b;
  if(mode=="all")return true;
  old=S.snapshot[x.address];if(typeof old=="undefined")return false;
  if(mode=="changed")return String(old)!=String(x.value);
  if(mode=="unchanged")return String(old)==String(x.value);
  a=Number(old);b=Number(x.value);
  if(mode=="increased")return isFinite(a)&&isFinite(b)&&b>a;
  if(mode=="decreased")return isFinite(a)&&isFinite(b)&&b<a;
  return true;
}
function renderResults(){
  var list=el("resultList");if(!list)return;
  var q=String(el("filter").value||"").toLowerCase(),mode=el("diffMode").value,out="",i,x;
  for(i=0;i<S.results.length;i++){
    x=S.results[i];
    if((x.address+" "+x.value).toLowerCase().indexOf(q)<0||!diffOK(x,mode))continue;
    out+='<div class="item"><div class="itemTop"><input type="checkbox" '+(x.selected?'checked ':'')+'onclick="H5X.selectResult(this,\''+esc(x.address)+'\')"><div class="itemMain"><div class="value">'+esc(x.value)+'</div><div class="addr">'+esc(x.address)+'</div><div class="type">'+esc(x.type)+'</div></div></div><div class="itemBtns"><button onclick="H5X.addWatch(\''+esc(x.address)+'\')">監視</button><button onclick="H5X.addBookmark(\''+esc(x.address)+'\')">保存</button></div></div>';
  }
  list.innerHTML=out||'<div class="empty">結果なし</div>';
}
function selectResult(box,a){var x=arrFind(S.results,function(r){return r.address==a});if(x)x.selected=!!box.checked}
function readValue(a,t){try{return hasH5()?String(call("getValue",a,t)):(arrFind(S.results,function(x){return x.address==a})||{}).value||"?"}catch(e){return "?"}}
function refreshResults(){var i;for(i=0;i<S.results.length;i++)S.results[i].value=readValue(S.results[i].address,S.results[i].type);renderResults();renderWatch()}
function snapshot(){var i;S.snapshot={};for(i=0;i<S.results.length;i++)S.snapshot[S.results[i].address]=S.results[i].value;alert("Snapshotを保存しました")}
function bulkSet(){var v=el("bulkValue").value,i,x;if(v===""){alert("新しい値を入力してください");return}for(i=0;i<S.results.length;i++){x=S.results[i];if(!x.selected)continue;try{if(hasH5())call("setValue",x.address,v,x.type);x.value=v}catch(e){}}renderResults()}
function addWatch(a){var x=arrFind(S.results,function(r){return r.address==a});if(x&&!exists(S.watch,function(w){return w.address==a})){S.watch.unshift({address:x.address,type:x.type});saveStore();renderWatch();alert("監視に追加しました")}}
function removeWatch(a){var n=[],i;for(i=0;i<S.watch.length;i++)if(S.watch[i].address!=a)n.push(S.watch[i]);S.watch=n;saveStore();renderWatch()}
function renderWatch(){var list=el("watchList");if(!list)return;var out="",i,x;for(i=0;i<S.watch.length;i++){x=S.watch[i];out+='<div class="item"><div class="value">'+esc(readValue(x.address,x.type))+'</div><div class="addr">'+esc(x.address)+'</div><div class="type">'+esc(x.type)+'</div><div class="itemBtns"><button onclick="H5X.removeWatch(\''+esc(x.address)+'\')">解除</button></div></div>'}list.innerHTML=out||'<div class="empty">監視なし</div>'}
function addBookmark(a){var x=arrFind(S.results,function(r){return r.address==a});if(x&&!exists(S.bookmarks,function(b){return b.address==a})){S.bookmarks.unshift({address:x.address,value:x.value,type:x.type});saveStore();renderBookmarks();alert("保存しました")}}
function removeBookmark(a){var n=[],i;for(i=0;i<S.bookmarks.length;i++)if(S.bookmarks[i].address!=a)n.push(S.bookmarks[i]);S.bookmarks=n;saveStore();renderBookmarks()}
function editBookmark(a){var x=arrFind(S.bookmarks,function(b){return b.address==a}),v;if(!x)return;v=prompt("新しい値",x.value);if(v===null)return;try{if(hasH5())call("setValue",x.address,v,x.type);x.value=v;saveStore();renderBookmarks()}catch(e){alert("編集エラー: "+e)}}
function renderBookmarks(){var list=el("bookmarkList");if(!list)return;var out="",i,x;for(i=0;i<S.bookmarks.length;i++){x=S.bookmarks[i];out+='<div class="item"><div class="value">'+esc(x.value)+'</div><div class="addr">'+esc(x.address)+'</div><div class="type">'+esc(x.type)+'</div><div class="itemBtns"><button onclick="H5X.editBookmark(\''+esc(x.address)+'\')">編集</button><button onclick="H5X.removeBookmark(\''+esc(x.address)+'\')">削除</button></div></div>'}list.innerHTML=out||'<div class="empty">保存なし</div>'}
function useHistory(i){var x=S.history[i];if(!x)return;el("value").value=x.value;el("type").value=x.type;el("rangeStart").value=x.start;el("rangeEnd").value=x.end;tab("results")}
function renderHistory(){var list=el("historyList");if(!list)return;var out="",i,x;for(i=0;i<S.history.length;i++){x=S.history[i];out+='<div class="item"><div class="value">'+esc(x.value)+'</div><div class="type">'+esc(x.type)+' / '+(new Date(x.ts)).toLocaleString()+'</div><div class="itemBtns"><button onclick="H5X.useHistory('+i+')">条件を戻す</button></div></div>'}list.innerHTML=out||'<div class="empty">履歴なし</div>'}

function loadModules(){
  var r=[],i,m;
  try{if(hasH5())r=call("getRangesList")||[]}catch(e){r=[]}
  if(!r.length&&!hasH5())r=[{name:"/Demo/AppDemo",start:"0x100000000",end:"0x100020000"}];
  S.modules=[];
  for(i=0;i<r.length;i++){m=r[i];S.modules.push({name:m.name||("module"+i),start:String(m.start),end:String(m.end)})}
  var sel=el("moduleSelect"),html="";
  for(i=0;i<S.modules.length;i++)html+='<option value="'+i+'">'+esc(basename(S.modules[i].name))+' | '+esc(S.modules[i].start)+'</option>';
  sel.innerHTML=html;moduleChanged();
}
function selectedModule(){var i=Number(el("moduleSelect").value)||0;return S.modules[i]||null}
function moduleChanged(){var m=selectedModule();el("moduleInfo").innerHTML=m?esc(m.name)+"\nBASE "+esc(m.start)+"\nEND  "+esc(m.end):"モジュールなし"}
function moduleOffsetText(a){var i,m,s,e;for(i=0;i<S.modules.length;i++){m=S.modules[i];s=toNum(m.start);e=toNum(m.end);if(isFinite(s)&&isFinite(e)&&a>=s&&a<e)return basename(m.name)+"+0x"+Math.floor(a-s).toString(16).toUpperCase()}return addrHex(a)}
function readU32(a){try{var v=Number(call("getValue",addrHex(a),"U32"));return isFinite(v)?v>>>0:0}catch(e){return 0}}
function readU8(a){try{var v=Number(call("getValue",addrHex(a),"U8"));return isFinite(v)?v&255:0}catch(e){return 0}}
function decode(w,a){
  w=w>>>0;
  if(w==0xD503201F)return "NOP";
  if(w==0xD65F03C0)return "RET";
  if((w&0xFFFFFC1F)==0xD61F0000)return "BR X"+((w>>>5)&31);
  if((w&0xFFFFFC1F)==0xD63F0000)return "BLR X"+((w>>>5)&31);
  if((w&0xFC000000)==0x14000000||(w&0xFC000000)==0x94000000){var im=w&0x03FFFFFF;if(im&0x02000000)im-=0x04000000;return ((w&0xFC000000)==0x94000000?"BL ":"B ")+moduleOffsetText(a+im*4)}
  return ".word "+hex8(w);
}
function readArmAt(start){
  var count=Math.max(1,Math.min(128,Number(el("armCount").value)||24)),i,a,w;
  start=Math.floor(start/4)*4;S.armStart=start;S.armRows=[];
  for(i=0;i<count;i++){a=start+i*4;w=hasH5()?readU32(a):[0xD503201F,0x94000002,0xD65F03C0][i%3];S.armRows.push({address:addrHex(a),word:w,asm:decode(w,a)})}
  el("armCurrent").innerHTML="開始: "+esc(moduleOffsetText(start))+" / "+esc(addrHex(start));renderArm();renderPatches();
}
function readArmOffset(){var m=selectedModule(),o=toNum(el("armOffset").value);if(!m){alert("モジュールを選択してください");return}if(!isFinite(o)){alert("Offsetは 0x1234 の形式で入力してください");return}readArmAt(toNum(m.start)+o)}
function readArmAbsolute(){var a=toNum(el("absoluteAddress").value);if(!isFinite(a)){alert("絶対アドレスを確認してください");return}readArmAt(a)}
function renderArm(){var list=el("armList");if(!list)return;var out="",i,r,p;for(i=0;i<S.armRows.length;i++){r=S.armRows[i];p=exists(S.patches,function(x){return x.address==r.address});out+='<div class="armRow '+(p?'patched':'')+'"><div class="armLine"><span class="armAddress">'+esc(moduleOffsetText(toNum(r.address)))+'</span><span class="armHex">'+esc(hex8(r.word))+'</span></div><div class="armAsm">'+esc(r.asm)+'</div><div class="armButtons"><button onclick="H5X.nop(\''+r.address+'\')">NOP</button><button onclick="H5X.editWord(\''+r.address+'\')">編集</button>'+(p?'<button onclick="H5X.restorePatch(\''+r.address+'\')">戻す</button>':'')+'</div></div>'}list.innerHTML=out||'<div class="empty">まだARM64を読み込んでいません</div>'}
function applyWord(address,newWord){
  var r=arrFind(S.armRows,function(x){return x.address==address}),old;if(!r)return;
  old=r.word>>>0;
  try{if(hasH5()&&call("setValue",address,String(newWord>>>0),"U32")===false){alert("書き込みに失敗しました");return}}
  catch(e){alert("書き込みエラー: "+e);return}
  if(!exists(S.patches,function(x){return x.address==address}))S.patches.unshift({address:address,original:old,current:newWord>>>0});
  else arrFind(S.patches,function(x){return x.address==address}).current=newWord>>>0;
  r.word=newWord>>>0;r.asm=decode(r.word,toNum(address));saveStore();renderArm();renderPatches();
}
function nop(a){applyWord(a,0xD503201F)}
function editWord(a){var r=arrFind(S.armRows,function(x){return x.address==a});if(!r)return;var v=prompt("新しいARM64命令 (8桁HEX)",hex8(r.word).replace("0x",""));if(v===null)return;if(!/^[0-9a-fA-F]{8}$/.test(v)){alert("8桁HEXで入力してください");return}applyWord(a,parseInt(v,16)>>>0)}
function restorePatch(a){var p=arrFind(S.patches,function(x){return x.address==a}),n=[],i,r;if(!p)return;try{if(hasH5())call("setValue",a,String(p.original>>>0),"U32")}catch(e){}for(i=0;i<S.patches.length;i++)if(S.patches[i].address!=a)n.push(S.patches[i]);S.patches=n;r=arrFind(S.armRows,function(x){return x.address==a});if(r){r.word=p.original>>>0;r.asm=decode(r.word,toNum(a))}saveStore();renderArm();renderPatches()}
function renderPatches(){var list=el("patchList");if(!list)return;var out="",i,p;for(i=0;i<S.patches.length;i++){p=S.patches[i];out+='<div class="item"><div class="patchInfo">'+esc(moduleOffsetText(toNum(p.address)))+'<br>'+esc(p.address)+'<br>'+esc(hex8(p.original))+' → '+esc(hex8(p.current))+'</div><div class="itemBtns"><button onclick="H5X.restorePatch(\''+p.address+'\')">復元</button></div></div>'}list.innerHTML=out||'<div class="empty">パッチなし</div>'}
function readHex(){var len=Math.max(16,Math.min(512,Number(el("hexLength").value)||128)),out="",off,i,a,b,ascii,bytes;if(!S.armStart){alert("先にARM64を読み込んでください");return}for(off=0;off<len;off+=16){bytes="";ascii="";for(i=0;i<16&&off+i<len;i++){a=S.armStart+off+i;b=hasH5()?readU8(a):((a+i)&255);bytes+=hex2(b)+" ";ascii+=(b>=32&&b<127)?String.fromCharCode(b):"."}out+=addrHex(S.armStart+off)+"  "+bytes+" "+ascii+"\n"}el("hexDump").textContent=out}

return {init:init,diag:diag,tab:tab,search:search,nearby:nearby,clearResults:clearResults,renderResults:renderResults,selectResult:selectResult,refreshResults:refreshResults,snapshot:snapshot,bulkSet:bulkSet,addWatch:addWatch,removeWatch:removeWatch,addBookmark:addBookmark,removeBookmark:removeBookmark,editBookmark:editBookmark,useHistory:useHistory,loadModules:loadModules,moduleChanged:moduleChanged,readArmOffset:readArmOffset,readArmAbsolute:readArmAbsolute,nop:nop,editWord:editWord,restorePatch:restorePatch,readHex:readHex};
})();

if(document.readyState=="loading")document.addEventListener("DOMContentLoaded",function(){try{H5X.init()}catch(e){alert("H5GGX初期化エラー: "+e)}});
else try{H5X.init()}catch(e){alert("H5GGX初期化エラー: "+e)}
