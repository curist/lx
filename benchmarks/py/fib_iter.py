import sys


MOD = 1_000_000_007


def fib_iter(n: int) -> int:
  if n <= 1:
    return n
  a, b = 0, 1
  for _ in range(2, n + 1):
    a, b = b, (a + b) % MOD
  return b


if __name__ == "__main__":
  n = int(sys.argv[1]) if len(sys.argv) > 1 else 0
  print(fib_iter(n))
