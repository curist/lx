function sieve(n) {
  if (n < 2) return 0;

  const isPrime = new Array(n + 1).fill(true);
  isPrime[0] = false;
  isPrime[1] = false;

  for (let i = 2; i * i <= n; i++) {
    if (isPrime[i]) {
      for (let j = i * i; j <= n; j += i) {
        isPrime[j] = false;
      }
    }
  }

  let count = 0;
  for (let i = 2; i <= n; i++) {
    if (isPrime[i]) {
      count++;
    }
  }
  return count;
}

const n = process.argv[2] ? parseInt(process.argv[2]) : 0;
console.log(sieve(n));
