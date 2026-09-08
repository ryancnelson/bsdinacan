-- Invoke with hs -c 'dofile(".../run.lua")'. No init.lua changes required.
local root=debug.getinfo(1,'S').source:sub(2):match('(.*/)')
local function readJSON(path)
 local f=assert(io.open(path,'rb'),'Cannot read '..path); local text=f:read('*a'); f:close()
 return assert(hs.json.decode(text),'Invalid JSON: '..path)
end
local cfg=readJSON(assert(macTestConfigPath or os.getenv('CANNEDBSD_MAC_TEST_CONFIG'),
 'Set macTestConfigPath to an external JSON configuration file'))
for _,name in ipairs({'app','python','state'}) do
 assert(type(cfg[name])=='string' and cfg[name]:sub(1,1)=='/','Config needs absolute '..name)
end
cfg.state=cfg.state:gsub('/+$','')
local slot=readJSON(cfg.state..'/slot/active.json')
local staged=slot.run_directory
assert(type(staged)=='string' and staged:sub(1,#cfg.state+5)==cfg.state..'/run-', 'Invalid staged run')
assert(not staged:sub(#cfg.state+2):find('/'),'Run must be a direct state child')
local manifest=readJSON(staged..'/manifest.json')
assert(manifest.run_directory==staged and manifest.boot_copy,'Stage with a clean --boot-seed first')
assert(not hs.fs.attributes(staged..'/acceptance.json'),'Stage a fresh run; this run was already checked')
cfg.prefs=staged..'/basilisk_prefs'; cfg.result=staged..'/shared/cannedbsd-result.txt'
local pf=assert(io.open(cfg.prefs,'rb'),'Stage with --native-template and --rom first')
local prefs=pf:read('*a'); pf:close()
local disks,exports={},{}
for line in prefs:gmatch('[^\r\n]+') do
 local kind,path=line:match('^%s*(%S+)%s+(.+)$')
 if kind=='disk' then disks[#disks+1]=path elseif kind=='extfs' then exports[#exports+1]=path end
end
assert(#disks==2 and disks[1]==staged..'/System.dsk' and disks[2]==staged..'/CannedBSD.dsk'
 and #exports==1 and exports[1]==staged..'/shared','Preferences do not match staged disks/export')
local resultFile=assert(io.open(cfg.result,'rb'),'Missing precreated guest result')
local initial=resultFile:read('*a'); resultFile:close()
assert(initial=='','Stage a fresh run; existing evidence must not be overwritten')
assert(not (macTestRun and macTestRun.active),'A test run already owns the UI')
assert(not hs.application.find('BasiliskII'),'Shut down the existing guest before running')
assert(hs.screenRecordingState() and hs.accessibilityState(),'Hammerspoon needs Screen Recording and Accessibility')
local R={active=true,start=hs.timer.secondsSinceEpoch(),events={},timers={},held=false}; macTestRun=R
R.dir=staged..'/automation-'..os.date('%Y%m%d-%H%M%S')
assert(hs.fs.mkdir(R.dir))
local function save(path,data) local f=assert(io.open(path,'wb')); f:write(data); f:close() end
local function log(s) R.events[#R.events+1]={seconds=hs.timer.secondsSinceEpoch()-R.start,state=s} end
local function after(delay,fn) R.timers[#R.timers+1]=hs.timer.doAfter(delay,function() if R.active then fn() end end) end
local function app()
 if R.pid then return hs.application.applicationForPID(R.pid) end
 return hs.application.find('BasiliskII')
end
local types=hs.eventtap.event.types
local function snap(path)
 local a=app(); local w=a and a:mainWindow(); if not w then return nil end
 local f=w:frame(); local i=w:snapshot(); if not i then return nil end
 -- Save at 2x instead of the window snapshot's 4x backing resolution.
 i:setSize({w=f.w,h=f.h},true):saveToFile(path); return f.table
end
local function finish(ok,why)
 if not R.active then return end
 if R.held then hs.eventtap.event.newMouseEvent(types.leftMouseUp,R.mouse):post(); R.held=false end
 R.active=false; for _,t in ipairs(R.timers) do t:stop() end
 if R.matcher then R.matcher:closeInput(); R.matcher:terminate() end
 log(why); save(R.dir..'/run.json',hs.json.encode({ok=ok,reason=why,elapsed=hs.timer.secondsSinceEpoch()-R.start,events=R.events,commit=manifest.commit,artifact_sha256=manifest.artifact_sha256,
 acceptance=staged..'/acceptance.json'},true))
 if not ok then snap(R.dir..'/failure.png') end
end
R.stop=function() finish(false,'Stopped; guest left running') end
local function focused(frame)
 local a=app(); local w=a and a:mainWindow(); if not w or not a:isFrontmost() then return false end
 local f=w:frame(); return f.x==frame.x and f.y==frame.y and f.w==frame.w and f.h==frame.h
end
local pending,buffer,beginBoot,startupTimer
buffer=''
R.matcher=hs.task.new(cfg.python,function(code) if R.active then finish(false,'Matcher exited: '..code) end end,function(_,out,err)
 if not R.active then return false end
 if err~='' then finish(false,'Matcher error: '..err); return false end
 buffer=buffer..out
 while buffer:find('\n') do
  local line,rest=buffer:match('^(.-)\n(.*)$'); buffer=rest
  local message=hs.json.decode(line)
  if not R.ready then
   if type(message)~='table' or message.ready~=true or message.protocol~=1 then
    finish(false,'Invalid matcher readiness handshake'); return false
   end
   R.ready=true; startupTimer:stop(); log('Matcher ready'); beginBoot()
  else
   local cb=pending; pending=nil
   if cb and R.active then
    if type(message)~='table' then finish(false,'Invalid matcher response'); return false end
    cb(message)
   end
  end
 end
 return true
end,{'-u',root..'match.py','--ready'})
local function find(name,cb)
 local frame=snap(R.dir..'/screen.png'); if not frame then after(.15,function() find(name,cb) end); return end
 pending=function(m)
  if m.error then finish(false,m.error)
  elseif m.found then
   if not focused(m.frame) then finish(false,'Focus or window frame changed'); return end
   log('Matched '..name); cb(m)
  else after(.15,function() find(name,cb) end) end
 end
 R.matcher:setInput(hs.json.encode({image=R.dir..'/screen.png',frame=frame,name=name})..'\n')
end
local function click(m,double,cb)
 if not focused(m.frame) then finish(false,'Focus or window frame changed before click'); return end
 R.mouse={x=m.x,y=m.y}; hs.eventtap.event.newMouseEvent(types.mouseMoved,R.mouse):post()
 after(.06,function()
  local function one(n)
   if not focused(m.frame) then finish(false,'Focus or window frame changed during click'); return end
   hs.eventtap.event.newMouseEvent(types.leftMouseDown,R.mouse):setProperty(hs.eventtap.event.properties.mouseEventClickState,n):post(); R.held=true
   after(.04,function()
    hs.eventtap.event.newMouseEvent(types.leftMouseUp,R.mouse):setProperty(hs.eventtap.event.properties.mouseEventClickState,n):post(); R.held=false
    if double and n==1 then after(.07,function() one(2) end) else after(.10,cb) end
   end)
  end
  one(1)
 end)
end
local function guestCommand(command,callback)
 local task=hs.task.new(cfg.python,function(code,out,err)
  if not R.active then return end
  if code~=0 then finish(false,'guest.py '..command..' rejected: '..out..err)
  else callback(out) end
 end,{root..'../guest.py',command,'--state',cfg.state})
 R.guestTask=task
 if not task:start() then finish(false,'Cannot start guest.py '..command) end
end
local function waitExit()
 if not app() then
  guestCommand('release',function() finish(true,'ALL PASS; screenshot saved; guest shut down; slot released') end)
 else after(.15,waitExit) end
end
local function shutdown()
 find('special',function(m)
  R.mouse={x=m.x,y=m.y}; hs.eventtap.event.newMouseEvent(types.mouseMoved,R.mouse):post()
  after(.06,function()
   if not focused(m.frame) then finish(false,'Focus or window frame changed before menu'); return end
   hs.eventtap.event.newMouseEvent(types.leftMouseDown,R.mouse):post(); R.held=true
   after(.12,function() find('menu',function(target)
    if not focused(target.frame) then finish(false,'Focus or window frame changed during menu'); return end
    R.mouse={x=target.x,y=target.y}; hs.eventtap.event.newMouseEvent(types.leftMouseDragged,R.mouse):post()
    after(.10,function()
     hs.eventtap.event.newMouseEvent(types.leftMouseUp,R.mouse):post(); R.held=false
     log('Selected Shut Down'); after(.15,waitExit)
    end)
   end) end)
  end)
 end)
end
local function evidence()
 local f=io.open(cfg.result,'rb'); local text=f and f:read('*a') or ''; if f then f:close() end
 local passes=0; for line in text:gmatch('[^\r\n]+') do if line:match('^PASS ') then passes=passes+1 end end
 if text:find('FAIL',1,true) then finish(false,'Guest test failure'); return end
 if passes~=8 or not text:match('ALL PASS\n?$') then after(.15,evidence); return end
 guestCommand('check',function()
  save(R.dir..'/cannedbsd-result.txt',text); snap(R.dir..'/test.png'); log('Fresh ALL PASS and screenshot saved')
  -- shell image was just matched; click its center before typing.
  click(R.shell,false,function()
   local a=app(); if not a or not a:isFrontmost() then finish(false,'Focus lost before exit'); return end
   for _,key in ipairs({'e','x','i','t','return'}) do hs.eventtap.keyStroke({},key,25000,a) end
   log('Typed exit'); after(.15,shutdown)
  end)
 end)
end
local function booted()
 local a=app(); if not a or not a:mainWindow() then after(.15,booted); return end
 R.pid=a:pid()
 -- Refuse a raced or unrelated guest even if it has the same application name.
 R.identity=hs.task.new('/bin/ps',function(code,out)
  if not R.active then return end
  if code~=0 or not out:find(cfg.prefs,1,true) then finish(false,'Guest process is not using staged preferences'); return end
  a:activate(); after(.15,function()
  find('trash',function(m) click(m,false,function()
   find('zzz',function(z) click(z,true,function()
    log('Launched zzz-run-tests'); find('shell',function(s) R.shell=s; evidence() end)
   end) end)
  end) end)
 end)
 end,{'-ww','-p',tostring(R.pid),'-o','command='})
 if not R.identity:start() then finish(false,'Cannot verify guest process') end
end
-- The stage command created the empty placeholder before boot. Never mutate it live.
log('Verified empty staged result')
beginBoot=function()
if hs.application.find('BasiliskII') then
 finish(false,'Another guest started during matcher initialization; guest was not launched'); return
end
R.launch=hs.task.new('/usr/bin/open',function(code) if code~=0 then finish(false,'Basilisk launch failed') end end,{'-na',cfg.app,'--args','--config',cfg.prefs})
assert(R.launch:start()); log('Boot started'); after(.15,booted)
after(cfg.timeout or 60,function() finish(false,'Timed out; guest left running for inspection') end)
end
-- Imports can block on evicted cloud files. No guest starts before readiness.
startupTimer=hs.timer.doAfter(cfg.startup_timeout or 30,function()
 finish(false,'Matcher startup timed out; guest was not launched. Keep runtime and state on local storage.')
end)
R.timers[#R.timers+1]=startupTimer
if not R.matcher:start() then finish(false,'Cannot start matcher; guest was not launched') end
