function fib(n) {
  if (n <= 1) {
    return n;
  } else {
    return fib(n - 1) + fib(n - 2);
  }
}

const n = process.argv[2] ? parseInt(process.argv[2]) : 0;
console.log(fib(n));
