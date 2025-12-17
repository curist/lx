local n = tonumber(arg[1]) or 0

local acc = 0
for i = 1, n do
  acc = acc + i
end

print(acc)
