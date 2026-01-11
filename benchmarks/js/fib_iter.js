const MOD = 1000000007;

function fibIter(n) {
  if (n <= 1) return n;
  let a = 0;
  let b = 1;
  for (let i = 2; i <= n; i++) {
    const c = (a + b) % MOD;
    a = b;
    b = c;
  }
  return b;
}

const n = process.argv[2] ? parseInt(process.argv[2]) : 0;
console.log(fibIter(n));
