'use strict';
const $ = id => document.getElementById(id);
const load = (key, fallback) => { try { return JSON.parse(localStorage.getItem(key)) ?? fallback; } catch { return fallback; } };
const save = (key, value) => { try { localStorage.setItem(key, JSON.stringify(value)); } catch { $('feedback').textContent = 'Your browser could not save this conversation.'; } };
const preferences = load('prospero-preferences', {theme:'midnight',accent:'gold',size:'normal',motion:false,contrast:false,temperature:0.7,length:0});
preferences.sounds ??= false; preferences.volume ??= 0.4;
for (const tokens of [8192, 16384, 32768, 65536, 131072]) {
    if (![...$('length').options].some(option => Number(option.value) === tokens))
        $('length').add(new Option(`${tokens} tokens`, String(tokens)));
}
let conversations = load('prospero-conversations', []);
if (!Array.isArray(conversations)) conversations = [];
conversations = conversations.filter(c => c && typeof c.id === 'string' && Array.isArray(c.messages));
let conversationSyncReady = load('prospero-sync-initialized', false), conversationSyncBusy = false;
let presetCatalog = [], modelFilter = 'all', downloadState = {state:'idle',active_preset:-1}, pendingDelete = '';
const kindNames = ['Text','Image','Audio','Voice'];
const modelKind = model => ['text-to-text','text-to-image','text-to-audio','text-to-speech'].indexOf(model.purpose || 'text-to-text');
let current = null, models = [], selectedModel = '', pendingModel = '', controller = null;
let key = ''; try { key = sessionStorage.getItem('prospero-key') || ''; } catch {}
function page(name) { for (const p of ['workspace','models','settings']) $(p).hidden = p !== name; document.querySelectorAll('[data-page]').forEach(b => { b.classList.toggle('selected', b.dataset.page === name); b.setAttribute('aria-current', b.dataset.page === name ? 'page' : 'false'); }); }
document.querySelectorAll('[data-page]').forEach(b => b.onclick = () => page(b.dataset.page));
document.querySelector('.brand').onclick = e => { e.preventDefault(); showWelcome(); };
function showWelcome(){if(controller){$('feedback').textContent='Stop the current response before leaving this conversation.';return;}current=null;page('workspace');$('chat').hidden=true;$('welcome').hidden=false;renderHistory();}
$('workspace-back').onclick=showWelcome;
function applyPreferences() { document.body.classList.toggle('daylight', preferences.theme === 'daylight'); document.body.dataset.accent = preferences.accent; document.body.classList.toggle('large', preferences.size === 'large'); document.body.classList.toggle('reduce-motion', preferences.motion); document.body.classList.toggle('contrast', preferences.contrast); drawSculpture(0); }
for (const [id, prop] of [['theme','theme'],['accent','accent'],['text-size','size'],['motion','motion'],['contrast','contrast'],['temperature','temperature'],['length','length'],['sounds','sounds'],['volume','volume']]) { const el = $(id); if (el.type === 'checkbox') el.checked = Boolean(preferences[prop]); else el.value = preferences[prop]; el.onchange = () => { preferences[prop] = el.type === 'checkbox' ? el.checked : ['temperature','length','context','volume'].includes(prop) ? Number(el.value) : el.value; $('temperature-value').textContent = preferences.temperature; save('prospero-preferences',preferences); applyPreferences(); }; }
$('temperature-value').textContent = preferences.temperature;
function drawMark() { const svg = document.querySelector('.mark'); for (let i=0;i<4;i++) { const r=10+i*3, start=-0.4+i*0.24,end=start+4.7; const p=document.createElementNS('http://www.w3.org/2000/svg','path'); p.setAttribute('d',`M ${25+r*Math.cos(start)} ${25+r*Math.sin(start)} A ${r} ${r} 0 1 1 ${25+r*Math.cos(end)} ${25+r*Math.sin(end)}`);p.setAttribute('fill','none');p.setAttribute('stroke','currentColor');p.setAttribute('stroke-width','1.4');svg.append(p); } }
function drawSculpture(time) { const ctx=$('sculpture').getContext('2d'), size=300,cx=360,cy=360,turn=0.46+0.025*Math.sin(time*0.35);ctx.clearRect(0,0,720,720);ctx.lineWidth=1.25;const color=getComputedStyle(document.body).getPropertyValue('--accent').trim(); for(let band=0;band<40;band++){const v=band*2*Math.PI/40;let lx,ly;for(let step=0;step<=72;step++){const u=step*2*Math.PI/72,r=size*(0.70+0.24*Math.cos(v)),x=r*Math.cos(u),y=r*Math.sin(u),z=size*0.24*Math.sin(v),ty=y*0.62-z*0.78,depth=y*0.78+z*0.62,px=cx+x*Math.cos(turn)-ty*Math.sin(turn),py=cy+x*Math.sin(turn)+ty*Math.cos(turn);if(step){ctx.globalAlpha=0.22+0.70*(depth/size+1)*0.5;ctx.strokeStyle=color;ctx.beginPath();ctx.moveTo(lx,ly);ctx.lineTo(px,py);ctx.stroke();}lx=px;ly=py;}}ctx.globalAlpha=1; }
let lastArt=0;function animate(t){if(t-lastArt>66&&!$('welcome').hidden&&!$('workspace').hidden&&!preferences.motion&&!matchMedia('(prefers-reduced-motion:reduce)').matches){drawSculpture(t/1000);lastArt=t;}requestAnimationFrame(animate);}drawMark();applyPreferences();requestAnimationFrame(animate);
async function api(path, options={}) { const response=await fetch(path,{...options,headers:{'Content-Type':'application/json',...(key?{Authorization:`Bearer ${key}`}:{})}});if(response.status===401){if(!$('key-dialog').open)$('key-dialog').showModal();throw Error('A connection key is required.');}if(!response.ok){let message=`Connection failed (${response.status}).`;try{const body=await response.json();message=body.error?.message||body.error||message;}catch{}throw Error(message);}return response; }
async function refreshModels(){
    try {
        const response=await api('/v1/models');
        const data=await response.json();
        models=(data.data||[]).filter(m=>typeof m.id==='string');
        const presets=await api('/api/models/presets');
        presetCatalog=(await presets.json()).data||[];
        if(!models.some(m=>m.id===selectedModel)) selectedModel=models[0]?.id||'';
        $('status').textContent=selectedModel?'PS5 · Ready':'No models installed';
        for(const [kind,id] of ['text-count','image-count','audio-count','voice-count'].entries()){
            const count=models.filter(m=>modelKind(m)===kind).length;
            $(id).textContent=count?`${count} ${count===1?'model':'models'} installed`:'No model yet';
        }
        renderModels();
        renderHistory();
        $('model-chip').textContent=current?.model||selectedModel||'Choose a model';
    }catch(error){$('status').textContent='PS5 · Disconnected';$('feedback').textContent=error.message;}
}
function renderModels(){
    const list=$('model-list');list.replaceChildren();
    const term=$('search').value.toLowerCase();
    const matches=(name,kind)=>name.toLowerCase().includes(term)&&(modelFilter==='all'||Number(modelFilter)===kind);
    let shown=0;
    for(const model of models){
        const kind=modelKind(model);
        if(!matches((model.name||model.id)+' '+model.id,kind))continue;
        ++shown;
        const button=document.createElement('button');button.className='glass model-card';
        const eyebrow=document.createElement('span');eyebrow.className='eyebrow';eyebrow.textContent=kindNames[kind]||'Text';
        const title=document.createElement('strong');title.textContent=model.name||model.id;
        const detail=document.createElement('small');detail.className=model.id===selectedModel?'active':'';
        detail.textContent=model.id===selectedModel?'● Active':'Installed on your PS5';
        button.append(eyebrow,title,detail);
        button.onclick=()=>{pendingModel=model.id;$('model-dialog').returnValue='';$('model-description').textContent=model.name||model.id;$('model-dialog').showModal();};
        list.append(button);
    }
    const busy=['searching','loading','downloading'].includes(downloadState.state);
    for(const preset of presetCatalog){
        if(preset.installed||models.some(model=>model.id===preset.id||model.id===preset.source_filename))continue;
        if(!matches(preset.name,preset.kind))continue;
        ++shown;
        const button=document.createElement('button');button.className='glass model-card preset-card';
        const eyebrow=document.createElement('span');eyebrow.className='eyebrow';eyebrow.textContent=kindNames[preset.kind]+' · PRESET';
        const title=document.createElement('strong');title.textContent=preset.name;
        const size=document.createElement('small');size.textContent=`${(preset.size/1073741824).toFixed(2)} GiB`;
        const action=document.createElement('small');action.className='active';
        action.textContent=downloadState.state==='downloading'&&downloadState.active_preset===preset.index?'Downloading…':'Download preset';
        button.disabled=busy;button.append(eyebrow,title,size,action);button.onclick=()=>downloadPreset(preset.index);list.append(button);
    }
    $('model-count').textContent=`${shown} shown / ${models.length} installed`;
    if(!shown){const empty=document.createElement('p');empty.className='muted';empty.textContent='No models match. Try another category or search.';list.append(empty);}
}
for(const button of document.querySelectorAll('[data-kind]')) button.onclick=()=>{
    modelFilter=button.dataset.kind;
    for(const tab of document.querySelectorAll('[data-kind]'))tab.classList.toggle('selected',tab===button);
    renderModels();
};
async function downloadPreset(index){
    try{await api('/api/models/presets',{method:'POST',body:JSON.stringify({index})});await pollDownloads();}
    catch(error){$('download-status').textContent=error.message;}
}
async function browseRepository(repository){$('download-status').textContent='Loading GGUF files…';try{await api('/api/models/browse',{method:'POST',body:JSON.stringify({repository})});await pollDownloads();}catch(error){$('download-status').textContent=error.message;}}
$('download-repo').onsubmit=async event=>{event.preventDefault();const query=$('model-query').value.trim();$('download-status').textContent='Searching public repositories…';$('browse-models').disabled=true;$('download-list').replaceChildren();try{await api('/api/models/search',{method:'POST',body:JSON.stringify({query})});await pollDownloads();}catch(error){$('download-status').textContent=error.message;}finally{$('browse-models').disabled=false;}};
function renderDownloadItems(data){const list=$('download-list');list.replaceChildren();for(let index=0;index<(data.items||[]).length;index++){const item=data.items[index],row=document.createElement('div');row.className='download-item';const detail=document.createElement('div'),name=document.createElement('strong'),size=document.createElement('small'),button=document.createElement('button');name.textContent=item.name;if(data.state==='search_ready'){size.textContent=`${Number(item.downloads||0).toLocaleString()} downloads`;button.textContent='Browse files';button.disabled=false;button.onclick=()=>browseRepository(item.name);}else{size.textContent=`${(item.size/1073741824).toFixed(2)} GiB · ${item.fit === "tight" ? "tight on memory (estimate)" : item.fit === "does_not_fit" ? "will not fit (estimate)" : "fits (estimate)"}`;button.textContent='Download';button.disabled=data.state!=='ready';button.onclick=()=>startModelDownload(index);}button.className='primary';detail.append(name,size);row.append(detail,button);list.append(row);}}
$('download-cancel').onclick=async()=>{
    if(!confirm('Cancel this download? What has been downloaded so far is removed.'))return;
    try{await api('/api/models/download',{method:'DELETE'});await pollDownloads();}
    catch(error){$('download-status').textContent=error.message;}
};
let downloadPoll;
async function pollDownloads(){
    clearTimeout(downloadPoll);
    try{
        const response=await api('/api/models/download');downloadState=await response.json();
        $('download-status').textContent=downloadState.status||'Ready';
        const bar=$('download-progress');bar.hidden=downloadState.state!=='downloading';
        bar.value=downloadState.total?Math.min(100,downloadState.completed*100/downloadState.total):0;
        const cancel=$('download-cancel');cancel.hidden=bar.hidden;cancel.disabled=!!downloadState.cancelling;cancel.textContent=downloadState.cancelling?'Cancelling…':'Cancel download';
        if(!bar.hidden&&!downloadState.cancelling)$('download-status').textContent=`Downloading… ${Math.floor(bar.value)}% · ${(downloadState.completed/1048576).toFixed(1)} / ${(downloadState.total/1048576).toFixed(1)} MiB`;
        renderDownloadItems(downloadState);renderModels();
        if(['searching','loading','downloading'].includes(downloadState.state)){downloadPoll=setTimeout(pollDownloads,1500);return;}
        if(downloadState.state==='complete')await refreshModels();
    }catch(error){$('download-status').textContent=error.message;}
}
async function startModelDownload(index){$('download-status').textContent='Starting verified download…';try{await api('/api/models/download',{method:'POST',body:JSON.stringify({index})});await pollDownloads();}catch(error){$('download-status').textContent=error.message;}}
$('search').oninput=renderModels;$('model-chip').onclick=()=>page('models');$('model-dialog').addEventListener('close',()=>{if($('model-dialog').returnValue==='use'){selectedModel=pendingModel;newConversation();renderModels();}});
function renderHistory(){
    const list=$('history');list.replaceChildren();
    const kind=$('history-kind').value, query=$('history-search').value.trim().toLocaleLowerCase();
    const shown=conversations.filter(conversation=>{
        const model=models.find(model=>model.id===conversation.model);
        const purpose=conversation.purpose||(model ? model.purpose||'text-to-text' : 'other');
        const searchable=[conversation.title,conversation.model,conversation.modelName,model?.name,...conversation.messages.map(message=>message?.content||'')].join('\n').toLocaleLowerCase();
        return (kind==='all'||purpose===kind)&&searchable.includes(query);
    });
    $('history-count').textContent=`${shown.length} of ${conversations.length} conversations`;
    for(const conversation of shown){
        const row=document.createElement('div');row.className='history-row';
        const open=document.createElement('button');open.textContent=conversation.title||'New conversation';open.classList.toggle('selected',conversation===current);
        open.onclick=()=>{if(controller){$('feedback').textContent='Stop the current response before switching conversations.';return;}current=conversation;openChat();renderMessages();renderHistory();closeHistory();};
        const remove=document.createElement('button');remove.className='delete-conversation';remove.textContent='×';remove.setAttribute('aria-label','Delete '+(conversation.title||'conversation'));
        remove.onclick=()=>{if(controller&&current===conversation){$('feedback').textContent='Stop the response before deleting this conversation.';return;}pendingDelete=conversation.id;$('delete-dialog').returnValue='';$('delete-description').textContent=conversation.title||'New conversation';$('delete-dialog').showModal();};
        row.append(open,remove);list.append(row);
    }
}
async function persistConversation(conversation){
    if(!conversation?.id)return;
    try{
        const response=await api('/api/conversations',{method:'POST',body:JSON.stringify(conversation)});
        const saved=await response.json();
        conversation._serverUpdatedUs=Number(saved.updated_us)||0;
        conversation.updatedAt=conversation._serverUpdatedUs/1000;
        save('prospero-conversations',conversations.slice(0,50));
    }catch(error){$('feedback').textContent=`Saved in this browser; console sync failed: ${error.message}`;}
}
async function syncConversations(){
    if(conversationSyncBusy||controller||document.visibilityState==='hidden')return;
    conversationSyncBusy=true;
    try{
        const response=await api('/api/conversations'),catalog=(await response.json()).data||[];
        const remoteById=new Map(catalog.map(item=>[item.id,item]));
        for(const conversation of [...conversations]){
            const remote=remoteById.get(conversation.id),localTime=Number(conversation.updatedAt||0)*1000;
            const known=Number(conversation._serverUpdatedUs||0),remoteTime=Number(remote?.updated_us||0);
            if((!remote&&(!conversationSyncReady||!known))||(remote&&localTime>known&&localTime>remoteTime)){
                const saved=await api('/api/conversations',{method:'POST',body:JSON.stringify(conversation)}).then(r=>r.json());
                conversation._serverUpdatedUs=Number(saved.updated_us)||0;conversation.updatedAt=conversation._serverUpdatedUs/1000;
                remoteById.set(conversation.id,saved);continue;
            }
            if(remoteTime>known&&remoteTime>=localTime){
                const detail=await api(`/api/conversations/${encodeURIComponent(conversation.id)}`).then(r=>r.json());
                const index=conversations.findIndex(item=>item.id===detail.id);if(index>=0)conversations[index]=detail;
                detail._serverUpdatedUs=Number(detail.updated_us)||0;detail.updatedAt=detail._serverUpdatedUs/1000;
            }else conversation._serverUpdatedUs=remoteTime;
        }
        for(const remote of remoteById.values())if(!conversations.some(item=>item.id===remote.id)){
            const detail=await api(`/api/conversations/${encodeURIComponent(remote.id)}`).then(r=>r.json());
            detail._serverUpdatedUs=Number(detail.updated_us)||0;detail.updatedAt=detail._serverUpdatedUs/1000;conversations.push(detail);
        }
        if(conversationSyncReady){
            const remoteIds=new Set(remoteById.keys());
            conversations=conversations.filter(item=>remoteIds.has(item.id));
        }
        conversationSyncReady=true;save('prospero-sync-initialized',true);
        conversations.sort((a,b)=>Number(b.updatedAt||0)-Number(a.updatedAt||0));
        save('prospero-conversations',conversations.slice(0,50));
        if(current){const updated=conversations.find(item=>item.id===current.id);if(updated&&updated!==current){current=updated;openChat();renderMessages();}}
        renderHistory();
    }catch(error){if(!String(error.message).includes('connection key'))$('feedback').textContent=`Conversation sync unavailable: ${error.message}`;}
    finally{conversationSyncBusy=false;}
}
$('delete-dialog').addEventListener('close',async()=>{
    if($('delete-dialog').returnValue!=='delete')return;
    if(controller&&current?.id===pendingDelete)return;
    try{await api(`/api/conversations/${encodeURIComponent(pendingDelete)}`,{method:'DELETE'});}
    catch(error){$('feedback').textContent=`Could not delete this conversation on the console: ${error.message}`;return;}
    conversations=conversations.filter(conversation=>conversation.id!==pendingDelete);
    save('prospero-conversations',conversations.slice(0,50));
    if(current?.id===pendingDelete)showWelcome();
    renderHistory();
});
function openChat(){page('workspace');$('welcome').hidden=true;$('chat').hidden=false;$('chat-title').textContent=current?.title||'A little room for big ideas.';$('model-chip').textContent=current?.model||selectedModel||'Choose a model';}
$('history-kind').onchange=renderHistory;
$('history-search').oninput=renderHistory;
function newConversation(){
    if(controller){$('feedback').textContent='Stop the current response before starting a new conversation.';return;}
    const model=models.find(model=>model.id===selectedModel);
    current={id:globalThis.crypto?.randomUUID?.()||String(Date.now()),title:'New conversation',model:selectedModel,modelName:model?.name||selectedModel,purpose:model ? model.purpose||'text-to-text' : 'other',messages:[],updatedAt:Date.now()};
    openChat();renderMessages();renderHistory();$('prompt').focus();
}
function startKind(kind){if(controller){$('feedback').textContent='Stop the current response before starting a new conversation.';return;}const candidates=models.filter(model=>modelKind(model)===kind);if(!candidates.length){modelFilter=String(kind);$('search').value='';for(const tab of document.querySelectorAll('[data-kind]'))tab.classList.toggle('selected',tab.dataset.kind===modelFilter);page('models');renderModels();return;}selectedModel=candidates.find(model=>model.id===selectedModel)?.id||candidates[0].id;newConversation();}
function closeHistory(){$('chat').classList.remove('show-history');$('history-toggle').setAttribute('aria-expanded','false');}
$('start').onclick=()=>startKind(0);for(const [kind,id] of ['converse','imagine','compose','speak'].entries())$(id).onclick=()=>startKind(kind);$('new').onclick=()=>{newConversation();closeHistory();};$('history-close').onclick=closeHistory;$('history-toggle').onclick=()=>{$('chat').classList.toggle('show-history');$('history-toggle').setAttribute('aria-expanded',String($('chat').classList.contains('show-history')));};
function extractMedia(text){const start=text.indexOf('/download0/');if(start<0)return null;let end=text.indexOf('.tga',start);if(end>=0)return{path:text.slice(start,end+4),kind:'image',before:text.slice(0,start)};end=text.indexOf('.wav',start);if(end>=0)return{path:text.slice(start,end+4),kind:'audio',before:text.slice(0,start)};return null;}
function decodeTGA(buffer){const d=new DataView(buffer);if(d.getUint8(2)!==2||d.getUint8(16)!==32)return null;const idLength=d.getUint8(0),colorMapType=d.getUint8(1),colorMapLength=d.getUint16(5,true),colorMapDepth=d.getUint8(7),width=d.getUint16(12,true),height=d.getUint16(14,true),topToBottom=(d.getUint8(17)&0x20)!==0;const colorMapBytes=colorMapType?Math.ceil(colorMapDepth/8)*colorMapLength:0,offset=18+idLength+colorMapBytes;if(width<=0||height<=0||offset+width*height*4>buffer.byteLength)return null;const out=new Uint8ClampedArray(width*height*4);for(let y=0;y<height;y++){const row=topToBottom?y:height-1-y;for(let x=0;x<width;x++){const si=offset+(y*width+x)*4,di=(row*width+x)*4;out[di]=d.getUint8(si+2);out[di+1]=d.getUint8(si+1);out[di+2]=d.getUint8(si);out[di+3]=d.getUint8(si+3);}}return{width,height,data:out};}
function renderImageMedia(el,blob){blob.arrayBuffer().then(buf=>{const decoded=decodeTGA(buf);if(!decoded)return;const canvas=document.createElement('canvas');canvas.width=decoded.width;canvas.height=decoded.height;canvas.className='message-media';canvas.getContext('2d').putImageData(new ImageData(decoded.data,decoded.width,decoded.height),0,0);el.append(canvas);canvas.toBlob(png=>{if(!png)return;const link=document.createElement('a');link.href=URL.createObjectURL(png);link.download='prosperoai-image.png';link.className='message-media-link';link.textContent='Download image';el.append(link);});}).catch(()=>{});}
function renderAudioMedia(el,blob){const url=URL.createObjectURL(blob);const audio=document.createElement('audio');audio.controls=true;audio.src=url;audio.className='message-media';el.append(audio);const link=document.createElement('a');link.href=url;link.download='prosperoai-audio.wav';link.className='message-media-link';link.textContent='Download audio';el.append(link);}
function appendMedia(el,media){api(media.path).then(r=>r.blob()).then(blob=>{if(media.kind==='image')renderImageMedia(el,blob);else renderAudioMedia(el,blob);}).catch(()=>{});}
function addMessage(message){const el=document.createElement('div');el.className=`message ${message.role==='user'?'user':'assistant'}`;const media=message.role!=='user'&&extractMedia(message.content||'');if(media){el.textContent=media.before.replace(/[\r\n]+$/,'');appendMedia(el,media);}else el.textContent=message.content||'';$('messages').append(el);return el;}
function renderMessages(){$('messages').replaceChildren();for(const message of current?.messages||[])addMessage(message);$('messages').scrollTop=$('messages').scrollHeight;}
$('prompt').oninput=()=>{$('prompt').style.height='auto';$('prompt').style.height=Math.min(180,$('prompt').scrollHeight)+'px';};$('prompt').onkeydown=e=>{if(e.key==='Enter'&&!e.shiftKey&&!e.isComposing&&matchMedia('(min-width:701px)').matches){e.preventDefault();$('composer').requestSubmit();}};
$('composer').onsubmit=async e=>{e.preventDefault();if(controller){controller.abort();return;}const text=$('prompt').value.trim();if(!text)return;if(!current)newConversation();if(!current.model){$('feedback').textContent='Choose an installed conversation model first.';page('models');return;}const conversation=current;if(!conversations.includes(conversation))conversations.unshift(conversation);conversation.messages.push({role:'user',content:text});if(conversation.messages.length===1)conversation.title=text.slice(0,70);conversation.updatedAt=Date.now();$('prompt').value='';$('prompt').style.height='auto';openChat();renderHistory();addMessage(conversation.messages.at(-1));const answer={role:'assistant',content:''};const el=addMessage(answer);controller=new AbortController();$('send').textContent='■';$('send').setAttribute('aria-label','Stop response');$('prompt').disabled=true;$('feedback').textContent='Thinking…';const start=performance.now();let received=false;try{const response=await api('/v1/chat/completions',{method:'POST',signal:controller.signal,body:JSON.stringify({model:conversation.model,messages:conversation.messages,stream:!(models.find(model=>model.id===conversation.model)?.purpose&&modelKind(models.find(model=>model.id===conversation.model))!==0),temperature:preferences.temperature,...(preferences.length?{max_tokens:preferences.length}:{})})});if(!response.headers.get('Content-Type')?.includes('text/event-stream')){
    const result=await response.json();answer.content=result.choices?.[0]?.message?.content||'';received=Boolean(answer.content);const media=extractMedia(answer.content);if(media){el.textContent=media.before.replace(/[\r\n]+$/,'');appendMedia(el,media);}else el.textContent=answer.content;
}else{
const reader=response.body.getReader(),decoder=new TextDecoder();let pending='';const event=block=>{const data=block.split('\n').filter(l=>l.startsWith('data:')).map(l=>l.slice(5).trim()).join('\n');if(!data||data==='[DONE]')return;const part=JSON.parse(data);if(part.error)throw Error(part.error.message||'The response could not be completed.');const token=part.choices?.[0]?.delta?.content;if(typeof token==='string'){received=true;answer.content+=token;el.textContent=answer.content;const log=$('messages');if(log.scrollHeight-log.scrollTop-log.clientHeight<180)log.scrollTop=log.scrollHeight;}};while(true){const {value,done}=await reader.read();pending+=decoder.decode(value||new Uint8Array(),{stream:!done}).replace(/\r\n/g,'\n');let split;while((split=pending.indexOf('\n\n'))>=0){event(pending.slice(0,split));pending=pending.slice(split+2);}if(done){if(pending.trim())event(pending);break;}}}
if(!received)throw Error('The model returned an empty response.');$('feedback').textContent=`Completed · ${((performance.now()-start)/1000).toFixed(1)}s`;}catch(error){$('feedback').textContent=error.name==='AbortError'?'Response stopped.':error.message;if(!received)el.remove();}finally{if(answer.content)conversation.messages.push(answer);conversation.updatedAt=Date.now();save('prospero-conversations',conversations.slice(0,50));controller=null;$('send').textContent='↑';$('send').setAttribute('aria-label','Send message');$('prompt').disabled=false;renderHistory();void persistConversation(conversation);}};
$('access').onclick=()=>{$('key-error').textContent='';$('key-dialog').showModal();};$('key-dialog').addEventListener('close',()=>{if($('key-dialog').returnValue==='connect'){key=$('key').value.trim();try{sessionStorage.setItem('prospero-key',key);}catch{}$('key').value='';refreshModels();}});
renderHistory();refreshModels();pollDownloads();
void syncConversations();setInterval(syncConversations,4000);

let audioContext;function sound(){if(!preferences.sounds)return;try{audioContext??=new (window.AudioContext||window.webkitAudioContext)();const osc=audioContext.createOscillator(),gain=audioContext.createGain();osc.type='sine';osc.frequency.value=620;gain.gain.setValueAtTime(preferences.volume*0.08,audioContext.currentTime);gain.gain.exponentialRampToValueAtTime(0.0001,audioContext.currentTime+0.09);osc.connect(gain);gain.connect(audioContext.destination);osc.start();osc.stop(audioContext.currentTime+0.1);}catch{}}
document.addEventListener('click',e=>{if(e.target.closest('button'))sound();});
for(const button of document.querySelectorAll('[data-category]'))button.onclick=()=>{for(const b of document.querySelectorAll('[data-category]')){const chosen=b===button;b.classList.toggle('selected',chosen);b.setAttribute('aria-selected',String(chosen));$('setting-'+b.dataset.category).hidden=!chosen;}};
$('preview-send').onclick=()=>{newConversation();$('prompt').value='Tell me a story.';$('prompt').focus();};

async function runtimeSettings(change) {
  const controls = ['runtime-context','runtime-thinking'].map($);
  controls.forEach(el => el.disabled = true);
  try {
    const response = await api('/api/settings/runtime', change ? {method:'POST',body:JSON.stringify(change)} : {});
    if (!response.ok) throw new Error('Could not save or load runtime settings.');
    const settings = await response.json();
    $('runtime-context').value = settings.context;
    $('runtime-thinking').checked = settings.thinking;
    $('runtime-feedback').textContent = '';
  } catch (error) { $('runtime-feedback').textContent = error.message; }
  finally { controls.forEach(el => el.disabled = false); }
}
for (const [id,key] of [['runtime-context','context'],['runtime-thinking','thinking']]) {
  $(id).onchange = () => runtimeSettings({[key]:key === 'thinking' ? $(id).checked : $(id).value});
}
document.querySelector('[data-page="settings"]').addEventListener('click', () => runtimeSettings());
