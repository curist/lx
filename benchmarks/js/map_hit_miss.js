function mapHitMiss(n) {
  const m = {};
  for (let i = 0; i < n; i++) {
    m[i] = i + 1;
  }
  let sum = 0;
  for (let i = 0; i < n; i++) {
    const k = i % 2 === 0 ? i : i + n;  // half misses
    const v = m[k];
    sum += (v !== undefined ? v : 1);
  }
  return sum;
}

const n = process.argv[2] ? parseInt(process.argv[2]) : 0;
console.log(mapHitMiss(n));
