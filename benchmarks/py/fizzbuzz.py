import sys


def fizzbuzz(n: int) -> int:
  acc = 0
  for i in range(1, n + 1):
    a = i % 3 == 0
    b = i % 5 == 0
    if a and b:
      acc += 3
    elif a:
      acc += 1
    elif b:
      acc += 2
  return acc


if __name__ == "__main__":
  n = int(sys.argv[1]) if len(sys.argv) > 1 else 0
  print(fizzbuzz(n))
