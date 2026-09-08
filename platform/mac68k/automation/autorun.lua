-- Host protocol only; Toolbox durability requires the real guest acceptance gate.
return function(ops)
 local function poll()
  local result,done=ops.read()
  if not result or not done then ops.fail('Cannot read autorun evidence'); return end
  if result:find('FAIL',1,true) or done=='FAIL\n' then ops.fail('Guest autorun test failure'); return end
  if done~='' and done~='PASS\n' then ops.fail('Invalid autorun completion'); return end
  if result:gsub('\r\n','\n'):gsub('\r','\n')~=ops.expected or done~='PASS\n' then
   ops.after(poll); return
  end
  ops.inspect(function()
   ops.waitClosed(function()
    ops.shutdown(function() ops.accept(function() ops.release() end) end)
   end)
  end)
 end
 return poll
end
