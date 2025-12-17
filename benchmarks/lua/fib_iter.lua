local n = tonumber(arg[1]) or 0
local MOD = 1000000007

if n <= 1 then
  print(n)
  return
end

local a = 0
local b = 1
for _ = 2, n do
  local c = (a + b) % MOD
  a = b
  b = c
end

print(b)
