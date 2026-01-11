import sys


def make_adder(x: int):
  def adder(y: int) -> int:
    return x + y
  return adder


def closure_heavy(n: int) -> int:
  closures = []
  for i in range(n):
    closures.append(make_adder(i))

  sum_val = 0
  for i in range(n):
    sum_val += closures[i](i)
  return sum_val


if __name__ == "__main__":
  n = int(sys.argv[1]) if len(sys.argv) > 1 else 0
  print(closure_heavy(n))
