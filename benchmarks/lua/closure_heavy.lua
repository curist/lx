local function makeAdder(x)
  return function(y)
    return x + y
  end
end

local function closureHeavy(n)
  local closures = {}
  for i = 0, n - 1 do
    closures[i + 1] = makeAdder(i)
  end

  local sum = 0
  for i = 0, n - 1 do
    sum = sum + closures[i + 1](i)
  end
  return sum
end

local n = tonumber(arg[1]) or 0
print(closureHeavy(n))
