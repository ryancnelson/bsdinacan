local calibration=dofile('platform/mac68k/automation/calibration.lua')
local frame={x=208,y=38,w=1024,h=796}
local screen={x=0,y=25,w=1440,h=875}
local cfg={window_origin={x=20,y=60},cursor_park={x=.4,y=.5}}
assert(calibration.validate(nil))
assert(calibration.validate(cfg))
local p=assert(calibration.plan(cfg,frame,screen))
assert(p.origin.x==20 and p.origin.y==60)
assert(p.cursor.x==429.6 and p.cursor.y==458)
local bads={false,{window_origin={x=0/0,y=60},cursor_park={x=.4,y=.5}},
 {window_origin={x=20,y=math.huge},cursor_park={x=.4,y=.5}},
 {window_origin={x=20,y=60},cursor_park={x=1,y=.5}},
 {window_origin={x=20,y=60},cursor_park={x=.4,y=-.1}},
 {window_origin={x=20,y=60}}}
for _,bad in ipairs(bads) do assert(not calibration.validate(bad)) end
assert(not calibration.plan(cfg,frame,{x=0,y=25,w=900,h=875}))
assert(not calibration.plan(cfg,{x=208,y=38,w=-1,h=796},screen))
local negative={window_origin={x=-1420,y=60},cursor_park={x=.4,y=.5}}
assert(calibration.plan(negative,frame,{x=-1440,y=25,w=1440,h=875}))
print('calibration geometry and rejection tests passed')
