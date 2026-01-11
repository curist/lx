import sys


def fib(n: int) -> int:
  if n <= 1:
    return n
  else:
    return fib(n - 1) + fib(n - 2)


if __name__ == "__main__":
  n = int(sys.argv[1]) if len(sys.argv) > 1 else 0
  print(fib(n))
