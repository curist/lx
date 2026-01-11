function fizzbuzz(n) {
  // checksum: 0=number, 1=Fizz, 2=Buzz, 3=FizzBuzz
  let acc = 0;
  for (let i = 1; i <= n; i++) {
    const a = i % 3 === 0;
    const b = i % 5 === 0;
    if (a && b) {
      acc += 3;
    } else if (a) {
      acc += 1;
    } else if (b) {
      acc += 2;
    }
  }
  return acc;
}

const n = process.argv[2] ? parseInt(process.argv[2]) : 0;
console.log(fizzbuzz(n));
