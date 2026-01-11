function arrayFill(n) {
  const arr = new Array(n);
  for (let i = 0; i < n; i++) {
    arr[i] = i;
  }
  let sum = 0;
  for (let i = 0; i < n; i++) {
    arr[i] = arr[i] + 1;
    sum += arr[i];
  }
  return sum;
}

const n = process.argv[2] ? parseInt(process.argv[2]) : 0;
console.log(arrayFill(n));
