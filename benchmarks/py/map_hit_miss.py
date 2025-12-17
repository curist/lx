import sys


def map_hit_miss(n: int) -> int:
  m = {i: i + 1 for i in range(n)}
  s = 0
  for i in range(n):
    k = i if i % 2 == 0 else i + n  # half misses
    s += m.get(k, 1)
  return s


if __name__ == "__main__":
  n = int(sys.argv[1]) if len(sys.argv) > 1 else 0
  print(map_hit_miss(n))
