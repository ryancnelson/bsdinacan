-- sessionProperties is documented to return a variable dictionary or nil.
-- macOS normally omits CGSSessionScreenIsLocked when unlocked: absence alone
-- is not a failure, but an active, fully logged-in console session is required.
return function(getProperties)
 if type(getProperties)~='function' then return false,'session API unavailable' end
 local ok,p=pcall(getProperties)
 if not ok then return false,'session query failed' end
 if type(p)~='table' then return false,'session data unavailable' end
 if p.CGSSessionScreenIsLocked==true then return false,'screen is locked' end
 if p.CGSSessionScreenIsLocked~=nil and p.CGSSessionScreenIsLocked~=false then
  return false,'lock state is malformed'
 end
 if p.kCGSSessionOnConsoleKey~=true then return false,'session is not confirmed on console' end
 if p.kCGSessionLoginDoneKey~=true then return false,'session login is not confirmed complete' end
 return true
end
