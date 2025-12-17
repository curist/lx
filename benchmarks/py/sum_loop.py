import sys


def sum_loop(n: int) -> int:
  acc = 0
  i = 1
  while i <= n:
    acc += i
    i += 1
  return acc


if __name__ == "__main__":
  n = int(sys.argv[1]) if len(sys.argv) > 1 else 0
  print(sum_loop(n))
