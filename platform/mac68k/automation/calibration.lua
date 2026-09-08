-- Geometry only: callers verify process ownership/focus before applying this plan.
local M={}
local function finite(n) return type(n)=='number' and n==n and math.abs(n)<math.huge end
local function point(p) return type(p)=='table' and finite(p.x) and finite(p.y) end
local function rect(r) return point(r) and finite(r.w) and finite(r.h) and r.w>0 and r.h>0 end
function M.validate(c)
 if c==nil then return true end
 if type(c)~='table' or not point(c.window_origin) or not point(c.cursor_park) then
  return nil,'calibration requires finite window_origin and cursor_park points'
 end
 local p=c.cursor_park
 if p.x<=0 or p.x>=1 or p.y<=0 or p.y>=1 then
  return nil,'cursor_park fractions must be strictly inside the window'
 end
 return true
end
function M.plan(c,frame,screen)
 local ok,why=M.validate(c)
 if not ok then return nil,why end
 if c==nil then return nil,'calibration is disabled' end
 if not rect(frame) or not rect(screen) then return nil,'invalid window or screen frame' end
 local o=c.window_origin
 if o.x<screen.x or o.y<screen.y or o.x+frame.w>screen.x+screen.w or o.y+frame.h>screen.y+screen.h then
  return nil,'calibrated window must fit its current screen'
 end
 return {origin={x=o.x,y=o.y},cursor={x=o.x+c.cursor_park.x*frame.w,y=o.y+c.cursor_park.y*frame.h},
  frame={x=o.x,y=o.y,w=frame.w,h=frame.h}}
end
return M
