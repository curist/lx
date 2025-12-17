local n = tonumber(arg[1]) or 0

local m = {}
for i = 0, n - 1 do
  m[i] = i + 1
end

local sum = 0
for i = 0, n - 1 do
  local k = (i % 2 == 0) and i or (i + n) -- half misses
  local v = m[k]
  if v == nil then v = 1 end
  sum = sum + v
end

print(sum)
