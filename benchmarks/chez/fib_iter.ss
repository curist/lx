(define args (command-line))
(define n (if (> (length args) 1) (string->number (list-ref args 1)) 0))

(define MOD 1000000007)

(define (fib-iter n)
  (if (<= n 1)
      n
      (let loop ((i 2) (a 0) (b 1))
        (if (> i n)
            b
            (let ((c (modulo (+ a b) MOD)))
              (loop (+ i 1) b c))))))

(display (fib-iter n))
(newline)
