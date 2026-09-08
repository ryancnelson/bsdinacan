-- Execute the actual driver with mocked host boundaries; no desktop or guest I/O.
local script='platform/mac68k/automation/run.lua'
local source=assert(io.open(script,'rb'))
local driver_text=source:read('*a'); source:close()
local function copy(t) local out={} for k,v in pairs(t) do out[k]=v end return out end
local function scenario(options)
 options=options or {}
 local s={tasks={},timers={},events={},saved={},mouse={},matches={},placements={},guest=false,front=true}
 local state='/local/state'; local run=state..'/run-calibration'
 s.prefs=run..'/basilisk_prefs'
 local calibration={window_origin={x=20,y=60},cursor_park={x=.4,y=.5}}
 if options.disabled then calibration=nil elseif options.config~=nil then calibration=options.config end
 local documents={config={app='/local/BasiliskII.app',python='/local/python',state=state,calibration=calibration},
  slot={run_directory=run},manifest={run_directory=run,boot_copy=true,commit='test'}}
 s.data={['/local/config']='config',[state..'/slot/active.json']='slot',
  [run..'/manifest.json']='manifest',[s.prefs]='disk '..run..'/System.dsk\ndisk '..run..'/CannedBSD.dsk\nextfs '..run..'/shared',
  [run..'/shared/cannedbsd-result.txt']='',[run..'/expected-result.txt']='fixture\nALL PASS\n'}
 local original={x=208,y=38,w=1024,h=796}
 s.frame=copy(original)
 local window={}
 function window:frame()
  s.events[#s.events+1]=#s.placements>0 and 'frame-readback' or 'frame'
  local f=copy(s.frame); f.table=copy(s.frame); return f
 end
 function window:screen()
  if options.no_screen then return nil end
  return {frame=function() return {x=0,y=25,w=1440,h=875} end}
 end
 function window:setTopLeft(point)
  s.events[#s.events+1]='place'; s.placements[#s.placements+1]=copy(point)
  if not options.reject_position then s.frame.x=point.x; s.frame.y=point.y end
  if options.resize then s.frame.w=s.frame.w+1 end
  if options.lose_focus_on_position then s.front=false end
  if options.lose_pid_on_position then s.missing_pid=true end
  return self
 end
 function window:snapshot()
  return {setSize=function(self) return self end,saveToFile=function() end}
 end
 local application={}
 function application:mainWindow() return not s.missing_window and window or nil end
 function application:pid() return 123 end
 function application:isFrontmost() return s.front end
 function application:activate() s.events[#s.events+1]='activate'; return true end
 local env=setmetatable({macTestConfigPath='/local/config'}, {__index=_G})
 s.env=env
 env.io={open=function(path,mode)
  if mode=='rb' then
   assert(s.data[path]~=nil,'unexpected read '..path)
   return {read=function() return s.data[path] end,close=function() end}
  end
  return {write=function(_,value) s.saved[path]=value end,close=function() end}
 end}
 local encoded=0
 env.hs={
  caffeinate={sessionProperties=function()
   return {kCGSSessionOnConsoleKey=true,kCGSessionLoginDoneKey=true,CGSSessionScreenIsLocked=s.locked or false}
  end},
  json={decode=function(value) return documents[value] end,encode=function(value)
   encoded=encoded+1; local key='encoded-'..encoded; documents[key]=value; return key
  end},
  fs={attributes=function() return nil end,mkdir=function() return true end},
  application={find=function() return s.guest and application or nil end,
   applicationForPID=function(pid) assert(pid==123,'wrong PID queried'); return not s.missing_pid and application or nil end},
  screenRecordingState=function() return true end,accessibilityState=function() return true end,
  eventtap={event={types={mouseMoved='move',leftMouseDown='down',leftMouseUp='up'},
   newMouseEvent=function(kind,point)
    return {post=function()
     s.events[#s.events+1]='cursor'; s.mouse[#s.mouse+1]={kind=kind,x=point.x,y=point.y}
    end}
   end}},
  timer={secondsSinceEpoch=function() return 1 end,doAfter=function(delay,fn)
   local t={delay=delay,fn=fn,stop=function(self) self.stopped=true end}
   s.timers[#s.timers+1]=t; return t
  end},
  task={new=function(path,callback,stream,args)
   local t={path=path,callback=callback,args=args or stream,
    stream=type(stream)=='function' and stream or nil}
   function t:start()
    self.started=true
    if path=='/usr/bin/open' then s.guest=true; s.events[#s.events+1]='launch' end
    if path=='/bin/ps' then s.events[#s.events+1]='identity-request'; s.identity=self end
    return true
   end
   function t:setInput(text)
    assert(self==s.tasks[1],'unexpected task input')
    s.events[#s.events+1]='match'; s.matches[#s.matches+1]=assert(documents[text:gsub('\n$','')])
   end
   function t:closeInput() self.closed=true end
   function t:terminate() self.terminated=true end
   s.tasks[#s.tasks+1]=t; return t
  end}
 }
 s.loaded,s.error=pcall(function() assert(load(driver_text,'@'..script,'t',env))() end)
 function s:tick()
  for _,t in ipairs(self.timers) do
   if t.delay==.15 and not t.fired and not t.stopped then t.fired=true; t.fn(); return end
  end
  error('no pending short driver timer')
 end
 function s:ready()
  documents.ready={ready=true,protocol=1}; self.tasks[1].stream(nil,'ready\n','')
 end
 function s:identify(code,text)
  assert(self.identity,'driver did not request process identity')
  self.events[#self.events+1]='identity-result'
  self.identity.callback(code or 0,text or self.prefs)
 end
 function s:receipt()
  for path,value in pairs(self.saved) do if path:match('/run.json$') then return documents[value] end end
 end
 return s
end
local function untouched(s)
 assert(s.data['/local/state/slot/active.json']=='slot','slot changed')
 assert(s.data['/local/state/run-calibration/shared/cannedbsd-result.txt']=='','evidence changed')
 for path in pairs(s.saved) do
  assert(not path:find('acceptance.json',1,true) and not path:find('/slot/',1,true),'acceptance/slot write')
 end
 for _,t in ipairs(s.tasks) do
  assert(t.path=='/local/python' or t.path=='/usr/bin/open' or t.path=='/bin/ps','unexpected task')
  if t.path=='/local/python' then assert(t.stream,'guest acceptance/release task started') end
  if t.path=='/usr/bin/open' then assert(not t.terminated,'guest launch task terminated') end
 end
end
local function no_actions(s)
 assert(#s.placements==0 and #s.mouse==0 and #s.matches==0,'action before identity/calibration permission')
end
local function prepared(options)
 local s=scenario(options); assert(s.loaded,s.error); no_actions(s)
 assert(not s.guest,'launch before matcher readiness')
 s:ready(); assert(s.guest); no_actions(s)
 s:tick(); assert(s.identity); no_actions(s)
 assert(s.identity.args[1]=='-ww' and s.identity.args[2]=='-p' and s.identity.args[3]=='123','identity checks wrong PID')
 return s
end
local function rejected(s,reason,placed)
 assert(not s.env.macTestRun.active,'rejection leaves controller active')
 local receipt=s:receipt()
 assert(receipt and receipt.ok==false and receipt.reason:find(reason,1,true),'missing rejection reason '..reason)
 assert(#s.placements==(placed or 0) and #s.mouse==0 and #s.matches==0,'rejection moved cursor or matched')
 assert(s.tasks[1].terminated,'rejection did not stop owned matcher')
 assert(s.guest,'rejection must leave guest for inspection')
 untouched(s)
end
local function first(s,event)
 for i,value in ipairs(s.events) do if value==event then return i end end
 error('missing event '..event)
end

local s=prepared()
s:identify(); no_actions(s); s:tick()
assert(s.env.macTestRun.active and #s.placements==1 and #s.mouse==1 and #s.matches==1)
assert(s.placements[1].x==20 and s.placements[1].y==60,'wrong window origin')
assert(s.mouse[1].kind=='move' and math.abs(s.mouse[1].x-429.6)<1e-9 and s.mouse[1].y==458,'wrong cursor parking/no-click contract')
assert(s.matches[1].name=='trash','calibration must precede first template')
assert(s.matches[1].frame.x==20 and s.matches[1].frame.y==60,'match did not use calibrated frame')
assert(first(s,'identity-result')<first(s,'place') and first(s,'place')<first(s,'frame-readback') and
 first(s,'frame-readback')<first(s,'cursor') and first(s,'cursor')<first(s,'match'),'calibration ordering')
untouched(s)

s=prepared({disabled=true}); s:identify(); s:tick()
assert(s.env.macTestRun.active and #s.placements==0 and #s.mouse==0 and #s.matches==1,'disabled calibration changed default path')
assert(s.matches[1].frame.x==208 and s.matches[1].frame.y==38,'disabled calibration moved window')
untouched(s)

for _,cfg in ipairs({false,{window_origin={x=math.huge,y=60},cursor_park={x=.4,y=.5}},
 {window_origin={x=20,y=60},cursor_park={x=0,y=.5}}}) do
 s=scenario({config=cfg})
 assert(not s.loaded and tostring(s.error):find('calibration') or
  not s.loaded and tostring(s.error):find('cursor_park'),'invalid config not rejected')
 assert(#s.tasks==0 and #s.timers==0 and not s.guest and not s.env.macTestRun,'invalid config allocated/launched driver')
 no_actions(s); untouched(s)
end
for _,identity in ipairs({{code=1,text='ps failed'},{code=0,text='/other/basilisk_prefs'}}) do
 s=prepared(); s:identify(identity.code,identity.text)
 rejected(s,'Guest process is not using staged preferences')
end
s=prepared(); s:identify(); s.front=false; s:tick(); rejected(s,'Focus lost before calibration')
s=prepared(); s:identify(); s.locked=true; s:tick(); rejected(s,'Desktop preflight: screen is locked')
s=prepared(); s:identify(); s.missing_window=true; s:tick(); rejected(s,'Focus lost before calibration')
s=prepared({no_screen=true}); s:identify(); s:tick(); rejected(s,'Window has no current screen')
s=prepared({config={window_origin={x=1000,y=60},cursor_park={x=.4,y=.5}}})
s:identify(); s:tick(); rejected(s,'calibrated window must fit its current screen')
for _,options in ipairs({{reject_position=true},{resize=true},{lose_focus_on_position=true},{lose_pid_on_position=true}}) do
 s=prepared(options); s:identify(); s:tick(); rejected(s,'Window calibration was not applied exactly',1)
end
print('actual driver calibration ordering, disabled path and rejection tests passed')
