function makeAdder(x) {
  return function(y) {
    return x + y;
  };
}

function closureHeavy(n) {
  const closures = [];
  for (let i = 0; i < n; i++) {
    closures.push(makeAdder(i));
  }

  let sum = 0;
  for (let i = 0; i < n; i++) {
    sum += closures[i](i);
  }
  return sum;
}

const n = process.argv[2] ? parseInt(process.argv[2]) : 0;
console.log(closureHeavy(n));
