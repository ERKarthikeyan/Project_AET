# Fixes for Reset Button & Dashboard Refresh Issues

## Problem 1: Reset Button Not Working
**Cause**: Modal pre-fill was not waiting for API response before showing

## Problem 2: Dashboard Not Refreshing  
**Cause**: Poll function had aggressive 3-second cooldown and faulty retry logic

---

## FIX #1: Replace RM() function in handleDashboard()

**Find this section** (around line 1850):
```javascript
h+="var _mn=null;";
h+="function RM(n){"
   "_mn=n;"
   ...
   "}).catch(function(){});"
   "var mo=document.getElementById('modal');if(mo)mo.style.display='flex';}";
```

**Replace with:**
```javascript
h+="var _mn=null;";
h+="function RM(n){"
   "_mn=n;"
   "var lb=document.getElementById('ml');if(lb)lb.textContent=n;"
   "var now=new Date(Date.now()+300000);"
   "var pad=function(x){return x<10?'0'+x:String(x);};"
   "var dv=now.getFullYear()+'-'+pad(now.getMonth()+1)+'-'+pad(now.getDate())"
   "+'T'+pad(now.getHours())+':'+pad(now.getMinutes());"
   "var de=document.getElementById('mdt');if(de)de.value=dv;"
   "var r0=document.getElementById('mr0');if(r0)r0.checked=true;"
   "var mn=document.getElementById('mname');if(mn)mn.value='';"
   "fetch('/api/status',{headers:{'ngrok-skip-browser-warning':'true'}})"
   ".then(function(r){return r.json();})"
   ".then(function(d){"
   "var mcyc=document.getElementById('mcyc');if(mcyc)mcyc.value=d.totalCycles||1000;"
   "var bto=document.getElementById('mbto');if(bto)bto.value=d.bootTO||120;"
   "var gap=document.getElementById('mgap');if(gap)gap.value=d.offGap||15;"
   "var ret=document.getElementById('mret');if(ret)ret.value=d.maxRetry||0;"
   "var bz=document.getElementById('mbz');if(bz)bz.value=d.buzzer?'1':'0';"
   "var mo=document.getElementById('modal');if(mo)mo.style.display='flex';})"
   ".catch(function(e){console.warn('prefill error:',e);"
   "var mo=document.getElementById('modal');if(mo)mo.style.display='flex';});"
   "}";
```

---

## FIX #2: Replace polling functions in handleDashboard()

**Find this section** (around line 1900-2000):
```javascript
h+="var _pollErr=0;";
h+="function sv(id,v){...}";
h+="function _setLive(ok){...}";
h+="function poll(){...}";
h+="var _fetching=false,_lastPollMs=0;";
h+="function pollSafe(){...}";
h+="setInterval(pollSafe,800);pollSafe();";
```

**Replace the polling section with:**
```javascript
h+="var _fetching=false,_lastPollMs=0,_pollErr=0;";
h+="function sv(id,v){var e=document.getElementById(id);if(e)e.textContent=(v!==null&&v!==undefined)?v:'';}";
h+="function _setLive(ok){var c=document.getElementById('conn');if(!c)return;c.style.background=ok?'#4ade80':'#f87171';if(ok)_pollErr=0;}";
h+="function poll(){"
   "return fetch('/api/status',{headers:{'ngrok-skip-browser-warning':'true'}})"
   ".then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json();})"
   ".then(function(d){"
   "_setLive(true);"
   "if(d.elapsed){sv('ov-el',d.elapsed);sv('ft-el',d.elapsed);}"
   "if(d.chipTemp!==undefined)sv('chipT',parseFloat(d.chipTemp).toFixed(1));"
   "var dts=d.duts;if(!dts||!dts.length)return;"
   "for(var _i=0;_i<dts.length;_i++){"
   "var dd=dts[_i];var ix=(dd.id!==undefined)?(dd.id-1):_i;"
   "try{"
   "var pct=dd.total>0?Math.round(dd.completed*100/dd.total):0;"
   "sv('cyc'+ix,dd.completed+'/'+dd.total+'  ('+pct+'%)');"
   "sv('pass'+ix,dd.pass);sv('err'+ix,dd.errors);sv('st'+ix,dd.state);sv('eta'+ix,dd.eta||'');"
   "if(dd.avgBoot&&parseFloat(dd.avgBoot)>0)sv('boot'+ix,parseFloat(dd.avgBoot).toFixed(1)+'s');"
   "var cp=document.getElementById('cp'+ix);if(cp)cp.textContent='PASS: '+dd.chkPass;"
   "var cf=document.getElementById('cf'+ix);"
   "if(cf){cf.textContent='FAIL: '+dd.chkFail;cf.className=dd.chkFail>0?'r':'g';}"
   "var ue=document.getElementById('usb'+ix);if(ue)ue.textContent=dd.usbActive?'USB ON':'USB Off';"
   "if(!dd.done&&!dd.stopped){var de=document.getElementById('del'+ix);if(de&&dd.dut_elapsed)de.textContent=dd.dut_elapsed;}"
   "var pf=document.getElementById('pf'+ix);if(pf)pf.style.width=pct+'%';"
   "var cd=document.getElementById('card'+ix);"
   "if(cd){var cc='card';if(dd.done)cc+=' done';else if(dd.stopped)cc+=' stopped';else if(dd.paused)cc+=' paused';else if(dd.errors>0)cc+=' err';cd.className=cc;}"
   "if(dd.last10){var sk=document.getElementById('sk'+ix);if(sk){"
   "var pp=dd.last10.split(','),sh='';"
   "for(var j=0;j<pp.length;j++){var cl=pp[j]==='1'?'p':(pp[j]==='0'?'f':'e');sh+='<div class=\"sp '+cl+'\"></div>';}"
   "sk.innerHTML=sh;}}}"
   "catch(ex){console.warn('DUT'+ix,ex);}}";
   "try{var wp=document.getElementById('wfpill');"
   "if(wp&&d.wifi_ssid){if(d.wifi_ok){"
   "wp.className='pill';wp.style.cssText='border:1px solid #86efac;background:#f0fdf4';"
   "wp.innerHTML='&#128246; <b style=\\'color:#15803d\\'>'+d.wifi_ssid+' ('+d.wifi_ip+')</b>';"
   "}else{wp.className='pill warn';wp.style.cssText='';"
   "wp.innerHTML='&#9888; <b>'+d.wifi_ssid+' not connected</b>';}}}catch(ex){}"
   "try{var np=document.getElementById('ngrokpill');"
   "if(np){if(d.ngrok_url&&d.ngrok_url.length>0){"
   "np.style.display='';"
   "np.innerHTML='&#127760; <b><a href=\"'+d.ngrok_url+'/dashboard\" target=\"_blank\" style=\"color:#3730a3\">'+d.ngrok_url+'/dashboard</a></b>';"
   "}else np.style.display='none';}}catch(ex){}"
   "var allDone=dts.length>0&&dts.every(function(x){return x.done||x.stopped;});"
   "if(allDone&&!d.running&&!document.getElementById('done-banner')){location.reload();}})"
   ".catch(function(e){_pollErr++;_setLive(false);console.warn('poll:',e);if(_pollErr>=10){_pollErr=0;location.reload();}});}"
   "function pollSafe(){"
   "if(_fetching)return;_fetching=true;_lastPollMs=Date.now();"
   "poll().finally(function(){_fetching=false;setTimeout(pollSafe,800);});}"
   "pollSafe();";
```

---

## FIX #3: Verify Modal HTML (should already be correct)

Find the reset modal opening:
```javascript
h+="<div id='modal' onclick='if(event.target===this)MC()'"
   " style='display:none;position:fixed;inset:0;background:rgba(0,0,0,.6);..."
```

Ensure `display:none;` is present (not `display:flex;`)

---

## Testing Steps

1. **Upload the modified sketch**
2. **Open browser to http://192.168.4.1**
3. **Start a test with at least 1 DUT**
4. **Dashboard should update every ~1 second** ✓
5. **Click "Reset DUT 1"** → Modal should appear with values pre-filled ✓
6. **Change the name or cycles** → Click "Start Test" ✓
7. **Check Serial Monitor** for `[CMD] reset_dut1:...` confirmation

---

## If Issues Persist

Check **Serial Monitor** for:
- `[ERROR]` messages
- `[CMD]` responses showing success/failure
- `[HTTP]` connection logs

Post the serial output and I can debug further.
