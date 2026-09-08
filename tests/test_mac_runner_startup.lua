-- No desktop is touched: only the startup/task boundary is simulated.
local script='platform/mac68k/automation/run.lua'
local function scenario(autorun,done,session)
 local tasks,timers,saved={},{},{}
 local properties=session or function() return {kCGSSessionOnConsoleKey=true,kCGSessionLoginDoneKey=true} end
 local state='/local/state'; local run=state..'/run-test'
 local documents={config={app='/local/BasiliskII.app',python='/local/python',state=state},
  slot={run_directory=run},manifest={run_directory=run,boot_copy=true,commit='test',autorun=autorun}}
 local data={['/local/config']='config',[state..'/slot/active.json']='slot',
  [run..'/manifest.json']='manifest',[run..'/basilisk_prefs']='disk '..run..'/System.dsk\ndisk '..run..'/CannedBSD.dsk\nextfs '..run..'/shared',
  [run..'/shared/cannedbsd-result.txt']='',[run..'/expected-result.txt']='fixture\nALL PASS\n'}
 if autorun then
  data[run..'/shared/cannedbsd-autorun.txt']=''
  data[run..'/shared/cannedbsd-screen.pict']=''
  data[run..'/shared/cannedbsd-done.txt']=done or ''
 end
 local env=setmetatable({macTestConfigPath='/local/config'}, {__index=_G})
 env.io={open=function(path,mode)
  if mode=='rb' then
   assert(data[path]~=nil,path); return {read=function() return data[path] end,close=function() end}
  end
  return {write=function(_,value) saved[path]=value end,close=function() end}
 end}
 env.hs={caffeinate={sessionProperties=properties},json={decode=function(value) return documents[value] end,encode=function(value) return value end},
  fs={attributes=function() return nil end,mkdir=function() return true end},
  application={find=function() return nil end},screenRecordingState=function() return true end,
  accessibilityState=function() return true end,eventtap={event={types={}}},
  timer={secondsSinceEpoch=function() return 1 end,doAfter=function(delay,fn)
   local t={delay=delay,fn=fn,stop=function(self) self.stopped=true end}; timers[#timers+1]=t; return t
  end},task={new=function(path,callback,stream,args)
   local t={path=path,callback=callback,stream=type(stream)=='function' and stream or nil,
    start=function(self) self.started=true; return true end,
    closeInput=function(self) self.closed=true end,terminate=function(self) self.terminated=true end}
   tasks[#tasks+1]=t; return t
  end}}
 assert(loadfile(script,'t',env))()
 return {env=env,tasks=tasks,timers=timers,documents=documents,saved=saved,data=data}
end
local function launches(s)
 local count=0; for _,t in ipairs(s.tasks) do if t.path=='/usr/bin/open' and t.started then count=count+1 end end
 return count
end
local s=scenario()
assert(launches(s)==0,'must not boot before matcher readiness')
s.documents.ready={ready=true,protocol=1}
s.tasks[1].stream(nil,'ready\n','')
assert(launches(s)==1,'ready matcher must launch once')
s.tasks[1].stream(nil,'ready\n','')
assert(launches(s)==1,'duplicate readiness must not launch twice')

s=scenario()
for _,t in ipairs(s.timers) do if t.delay==30 then t.fn() end end
assert(not s.env.macTestRun.active,'unresponsive matcher must fail startup')
assert(launches(s)==0,'startup timeout must leave guest unlaunched')
assert(s.tasks[1].terminated,'timeout must terminate only its owned matcher')
s.documents.ready={ready=true,protocol=1}
s.tasks[1].stream(nil,'ready\n','')
assert(launches(s)==0,'late readiness after timeout must not launch')

s=scenario()
s.documents.bad={ready=true,protocol=999}
s.tasks[1].stream(nil,'bad\n','')
assert(not s.env.macTestRun.active and launches(s)==0,'bad handshake must fail closed')
s=scenario()
s.env.hs.application.find=function() return {mainWindow=function() return nil end} end
s.documents.ready={ready=true,protocol=1}
s.tasks[1].stream(nil,'ready\n','')
assert(not s.env.macTestRun.active and launches(s)==0,'raced guest must not cause second launch')
print('matcher startup gate passed')

s=scenario(true)
assert(launches(s)==0,'autorun must still wait for matcher readiness')
s.documents.ready={ready=true,protocol=1}; s.tasks[1].stream(nil,'ready\n','')
assert(launches(s)==1,'empty precreated autorun files may boot')
local ok,message=pcall(function() scenario(true,'PASS\n') end)
assert(not ok and tostring(message):find('fresh empty autorun files',1,true),
 'stale done token must reject startup before any guest launch')

-- These are session/startup tests, not simulated guest acceptance.
local function rejected(s,reason)
 assert(not s.env.macTestRun.active,'desktop preflight must fail closed: '..reason)
 assert(launches(s)==0,'desktop preflight must not launch a guest: '..reason)
 assert(#s.tasks<=1,'preflight must not start acceptance or release tasks')
 if s.tasks[1] then assert(s.tasks[1].path=='/local/python' and s.tasks[1].stream,'only matcher task is permitted') end
 assert(s.data['/local/state/slot/active.json']=='slot','claimed slot must be preserved')
 assert(s.data['/local/state/run-test/shared/cannedbsd-result.txt']=='','guest evidence must be preserved')
 local receipt
 for path,value in pairs(s.saved) do
  assert(not path:find('acceptance.json',1,true),'preflight cannot create acceptance')
  assert(not path:find('failure.png',1,true),'nil snapshot is not screenshot evidence')
  assert(not path:find('/slot/',1,true),'preflight cannot overwrite the slot')
  if path:match('/run.json$') then receipt=value end
 end
 assert(receipt and receipt.ok==false and receipt.reason:find(reason,1,true),'failure must explain preflight rejection')
end
local unlocked={kCGSSessionOnConsoleKey=true,kCGSessionLoginDoneKey=true}
local cases={
 {name='screen is locked',properties={CGSSessionScreenIsLocked=true,kCGSSessionOnConsoleKey=true,kCGSessionLoginDoneKey=true}},
 {name='session data unavailable',get=function() return nil end},
 {name='session query failed',get=function() error('query unavailable') end},
 {name='session data unavailable',properties='invalid'},
 {name='session data unavailable',properties=false},
 {name='lock state is malformed',properties={CGSSessionScreenIsLocked='false',kCGSSessionOnConsoleKey=true,kCGSessionLoginDoneKey=true}},
 {name='session is not confirmed on console',properties={kCGSessionLoginDoneKey=true}},
 {name='session is not confirmed on console',properties={kCGSSessionOnConsoleKey=false,kCGSessionLoginDoneKey=true}},
 {name='session login is not confirmed complete',properties={kCGSSessionOnConsoleKey=true}},
 {name='session login is not confirmed complete',properties={kCGSSessionOnConsoleKey=true,kCGSessionLoginDoneKey=false}}
}
for _,case in ipairs(cases) do
 s=scenario(nil,nil,case.get or function() return case.properties end)
 rejected(s,case.name)
 assert(#s.tasks==0,'initial rejection must not even start the matcher')
end
-- Missing lock key is the normal unlocked dictionary, already exercised above.
-- Explicit false is also accepted; present nonboolean values are not.
s=scenario(nil,nil,function() return {CGSSessionScreenIsLocked=false,kCGSSessionOnConsoleKey=true,kCGSessionLoginDoneKey=true} end)
s.documents.ready={ready=true,protocol=1}; s.tasks[1].stream(nil,'ready\n','')
assert(launches(s)==1,'explicit unlocked active login must launch after readiness')
for _,case in ipairs(cases) do
 s=scenario(nil,nil,function() return unlocked end)
 s.env.hs.caffeinate.sessionProperties=case.get or function() return case.properties end
 s.documents.ready={ready=true,protocol=1}; s.tasks[1].stream(nil,'ready\n','')
 rejected(s,case.name)
 assert(s.tasks[1].terminated,'recheck rejection must stop its owned matcher')
end
s=scenario(); s.env.hs.caffeinate=nil
s.documents.ready={ready=true,protocol=1}; s.tasks[1].stream(nil,'ready\n','')
rejected(s,'session API unavailable')
print('desktop session preflight passed')
