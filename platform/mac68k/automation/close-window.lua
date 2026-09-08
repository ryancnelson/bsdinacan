-- Manual failure-inspection action. Never starts or terminates an emulator.
-- Set macTestConfigPath and macClosePID, then dofile this file.
local root = debug.getinfo(1, 'S').source:sub(2):match('(.*/)')
local function readJSON(path)
    local file = assert(io.open(path, 'rb'), 'Cannot read ' .. path)
    local value = file:read('*a'); file:close()
    return assert(hs.json.decode(value), 'Invalid JSON: ' .. path)
end
local cfg = readJSON(assert(macTestConfigPath, 'Set macTestConfigPath'))
assert(type(cfg.python) == 'string' and cfg.python:sub(1, 1) == '/', 'Absolute Python path required')
assert(type(cfg.state) == 'string' and cfg.state:sub(1, 1) == '/', 'Absolute state path required')
assert(type(macClosePID) == 'number' and macClosePID > 0 and macClosePID % 1 == 0,
       'Set macClosePID to the inspected Basilisk II PID')
assert(not (macTestRun and macTestRun.active), 'Stop the main runner before recovery')
assert(not (macCloseRun and macCloseRun.active), 'A close action is already active')
assert(hs.screenRecordingState() and hs.accessibilityState(), 'Hammerspoon permissions required')
cfg.state = cfg.state:gsub('/+$', '')
local staged = readJSON(cfg.state .. '/slot/active.json').run_directory
assert(type(staged) == 'string' and staged:sub(1, #cfg.state + 5) == cfg.state .. '/run-'
       and not staged:sub(#cfg.state + 2):find('/'), 'Invalid staged run')
local manifest = readJSON(staged .. '/manifest.json')
assert(manifest.run_directory == staged, 'Manifest does not own the staged run')
local prefs = staged .. '/basilisk_prefs'
local R = {active=false, pid=macClosePID, timers={}, held=false}
macCloseRun = R
R.dir = staged .. '/recovery-close-' .. os.date('%Y%m%d-%H%M%S')
assert(hs.fs.mkdir(R.dir), 'Recovery evidence directory already exists')
R.active=true
local function application()
    return hs.application.applicationForPID(R.pid)
end
local function invariant(frame)
    local app = application()
    local window = app and app:mainWindow()
    if not window or not app:isFrontmost() or (macTestRun and macTestRun.active) then return false end
    local f = window:frame()
    return not frame or (f.x == frame.x and f.y == frame.y and f.w == frame.w and f.h == frame.h)
end
local function snapshot(path)
    assert(invariant(), 'Guest must remain frontmost')
    local window = application():mainWindow()
    local frame = window:frame()
    local image = assert(window:snapshot(), 'Cannot capture guest window')
    assert(image:setSize({w=frame.w, h=frame.h}, true):saveToFile(path), 'Cannot save screenshot')
    return frame.table
end
local function saveRecord(reason)
    local file = assert(io.open(R.dir .. '/action.json', 'wb'))
    file:write(hs.json.encode({action='close-cannedbsd-window', reason=reason, pid=R.pid,
        commit=manifest.commit, artifact_sha256=manifest.artifact_sha256,
        click_sent=R.clickSent or false, match=R.match}, true)); file:close()
end
local types = hs.eventtap.event.types
local function finish(reason)
    if not R.active then return end
    R.active = false
    for _,timer in ipairs(R.timers) do timer:stop() end
    if R.held then hs.eventtap.event.newMouseEvent(types.leftMouseUp, R.mouse):post(); R.held=false end
    if R.matcherStarted then R.matcher:closeInput(); R.matcher:terminate() end
    saveRecord(reason)
end
R.stop = function() finish('Stopped; inspect retained evidence') end
local function after(delay, callback)
    R.timers[#R.timers + 1] = hs.timer.doAfter(delay, function()
        if not R.active then return end
        local ok, error = pcall(callback)
        if not ok then finish(tostring(error)) end
    end)
end
local function click(match)
    assert(invariant(match.frame), 'Guest focus/frame changed before click')
    R.match = match; R.mouse = {x=match.x, y=match.y}
    hs.eventtap.event.newMouseEvent(types.mouseMoved, R.mouse):post()
    after(.06, function()
        assert(invariant(match.frame), 'Guest focus/frame changed during click')
        hs.eventtap.event.newMouseEvent(types.leftMouseDown, R.mouse):post(); R.held=true
        after(.04, function()
            hs.eventtap.event.newMouseEvent(types.leftMouseUp, R.mouse):post(); R.held=false
            R.clickSent=true
            after(.25, function()
                snapshot(R.dir .. '/after.png')
                finish('Close click delivered; inspect after.png before further actions')
            end)
        end)
    end)
end
local buffer = ''
local function response(line)
    local message = assert(hs.json.decode(line), 'Invalid matcher JSON')
    if not R.ready then
        assert(message.ready == true and message.protocol == 1, 'Invalid matcher readiness')
        R.ready=true
        local frame = snapshot(R.dir .. '/before.png')
        R.matcher:setInput(hs.json.encode({image=R.dir .. '/before.png', frame=frame,
            name='cannedbsd-close'}) .. '\n')
    else
        assert(not R.match, 'Unexpected duplicate matcher reply')
        assert(message.found == true, message.error or 'No unique cannedBSD close target; no click sent')
        click(message)
    end
end
R.matcher = hs.task.new(cfg.python, function(code)
    if R.active then finish('Matcher exited: ' .. code) end
end, function(_, out, err)
    if not R.active then return false end
    if err ~= '' then finish('Matcher error: ' .. err); return false end
    buffer = buffer .. out
    while buffer:find('\n') do
        local line, rest = buffer:match('^(.-)\n(.*)$'); buffer=rest
        local ok, error = pcall(response, line)
        if not ok then finish(tostring(error)); return false end
    end
    return true
end, {'-u', root .. 'match.py', '--ready'})
-- Validate the inspected PID against the active staged preferences before any input.
R.identity = hs.task.new('/bin/ps', function(code, out)
    if not R.active then return end
    if code ~= 0 or not out:find('BasiliskII', 1, true) or not out:find(prefs, 1, true) then
        finish('PID is not the staged Basilisk II process'); return
    end
    if not invariant() then finish('Inspected guest is not frontmost'); return end
    R.matcherStarted=not not R.matcher:start()
    if not R.matcherStarted then finish('Cannot start matcher') end
end, {'-ww', '-p', tostring(R.pid), '-o', 'command='})
after(cfg.startup_timeout or 30, function() finish('Close action timed out; inspect retained evidence') end)
if not R.identity:start() then finish('Cannot verify guest process') end
