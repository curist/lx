import sys


def array_fill(n: int) -> int:
  arr = [0] * n
  for i in range(n):
    arr[i] = i
  s = 0
  for i in range(n):
    arr[i] += 1
    s += arr[i]
  return s


if __name__ == "__main__":
  n = int(sys.argv[1]) if len(sys.argv) > 1 else 0
  print(array_fill(n))
