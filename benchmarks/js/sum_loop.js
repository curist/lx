function sumLoop(n) {
  let acc = 0;
  for (let i = 1; i <= n; i++) {
    acc += i;
  }
  return acc;
}

const n = process.argv[2] ? parseInt(process.argv[2]) : 0;
console.log(sumLoop(n));
