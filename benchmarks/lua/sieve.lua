local function sieve(n)
  if n < 2 then
    return 0
  end

  local isPrime = {}
  for i = 0, n do
    isPrime[i] = true
  end
  isPrime[0] = false
  isPrime[1] = false

  local i = 2
  while i * i <= n do
    if isPrime[i] then
      local j = i * i
      while j <= n do
        isPrime[j] = false
        j = j + i
      end
    end
    i = i + 1
  end

  local count = 0
  for i = 2, n do
    if isPrime[i] then
      count = count + 1
    end
  end
  return count
end

local n = tonumber(arg[1]) or 0
print(sieve(n))
