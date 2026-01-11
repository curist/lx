local function fib(n)
  if n <= 1 then
    return n
  else
    return fib(n - 1) + fib(n - 2)
  end
end

local n = tonumber(arg[1]) or 0
print(fib(n))
