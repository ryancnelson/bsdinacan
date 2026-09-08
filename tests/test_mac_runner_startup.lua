-- No desktop is touched: only the startup/task boundary is simulated.
local script='platform/mac68k/automation/run.lua'
local function scenario()
 local tasks,timers,saved={},{},{}
 local state='/local/state'; local run=state..'/run-test'
 local documents={config={app='/local/BasiliskII.app',python='/local/python',state=state},
  slot={run_directory=run},manifest={run_directory=run,boot_copy=true,commit='test'}}
 local data={['/local/config']='config',[state..'/slot/active.json']='slot',
  [run..'/manifest.json']='manifest',[run..'/basilisk_prefs']='disk '..run..'/System.dsk\ndisk '..run..'/CannedBSD.dsk\nextfs '..run..'/shared',
  [run..'/shared/cannedbsd-result.txt']='',[run..'/expected-result.txt']='fixture\nALL PASS\n'}
 local env=setmetatable({macTestConfigPath='/local/config'}, {__index=_G})
 env.io={open=function(path,mode)
  if mode=='rb' then
   assert(data[path]~=nil,path); return {read=function() return data[path] end,close=function() end}
  end
  return {write=function(_,value) saved[path]=value end,close=function() end}
 end}
 env.hs={json={decode=function(value) return documents[value] end,encode=function(value) return value end},
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
 return {env=env,tasks=tasks,timers=timers,documents=documents,saved=saved}
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
