-- Inject host operations to prove sequencing; no emulator or Toolbox is mocked.
local start=dofile('platform/mac68k/automation/autorun.lua')
local function scenario()
 local s={result='',done='',events={}}
 local function mark(name) s.events[#s.events+1]=name end
 s.poll=start({expected='expected\nALL PASS\n',
  read=function() return s.result,s.done end,
  after=function(fn) s.next=fn; mark('wait') end,
  fail=function(why) s.failure=why; mark('fail') end,
  inspect=function(cb) s.inspected=cb; mark('inspect') end,
  waitClosed=function(cb) s.closed=cb; mark('observe closure') end,
  accept=function(cb) s.accepted=cb; mark('accept') end,
  shutdown=function(cb) s.exited=cb; mark('shutdown') end,
  release=function() mark('release') end})
 return s
end
local s=scenario()
s.poll(); assert(table.concat(s.events,',')=='wait')
s.done='PASS\n'; s.next(); assert(s.events[#s.events]=='wait','token alone cannot advance')
s.result='expected\nALL PASS\n'; s.next()
assert(s.events[#s.events]=='inspect' and not s.closed and not s.accepted)
-- Without successful decoder/freshness validation there is no closure/acceptance/shutdown.
s.inspected(); assert(s.events[#s.events]=='observe closure' and not s.accepted)
-- A completion token and screenshot cannot substitute for observed app closure.
s.closed(); assert(s.events[#s.events]=='shutdown' and not s.accepted)
-- No receipt if guest shutdown hangs or fails.
s.exited(); assert(s.events[#s.events]=='accept')
assert(s.events[#s.events]~='release','receipt validation must complete first')
s.accepted(); assert(s.events[#s.events]=='release')
for _,entry in ipairs({{'expected\nALL PASS\n','FAIL\n'},{'FAILED\n',''},
                       {'expected\nALL PASS\n','PASS'},{'expected\nALL PASS\n','garbage'}}) do
 s=scenario(); s.result=entry[1]; s.done=entry[2]; s.poll()
 assert(s.failure and not s.inspected and table.concat(s.events,',')=='fail')
end
s=scenario(); s.result='expected\nALL PASS\n'; s.poll()
assert(not s.inspected,'missing completion must keep app open without acceptance')
s=scenario(); s.result=nil; s.poll(); assert(s.failure and not s.inspected)
print('autorun driver sequencing passed')
