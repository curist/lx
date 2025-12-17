local n = tonumber(arg[1]) or 0

local acc = 0
for i = 1, n do
  local a = (i % 3) == 0
  local b = (i % 5) == 0
  if a and b then
    acc = acc + 3
  elseif a then
    acc = acc + 1
  elseif b then
    acc = acc + 2
  end
end

print(acc)
