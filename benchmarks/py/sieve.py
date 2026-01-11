import sys


def sieve(n: int) -> int:
  if n < 2:
    return 0

  is_prime = [True] * (n + 1)
  is_prime[0] = False
  is_prime[1] = False

  i = 2
  while i * i <= n:
    if is_prime[i]:
      j = i * i
      while j <= n:
        is_prime[j] = False
        j += i
    i += 1

  count = 0
  for i in range(2, n + 1):
    if is_prime[i]:
      count += 1
  return count


if __name__ == "__main__":
  n = int(sys.argv[1]) if len(sys.argv) > 1 else 0
  print(sieve(n))
