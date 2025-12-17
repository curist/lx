local n = tonumber(arg[1]) or 0

local arr = {}
for i = 1, n do
  arr[i] = i - 1
end

local sum = 0
for i = 1, n do
  arr[i] = arr[i] + 1
  sum = sum + arr[i]
end

print(sum)
