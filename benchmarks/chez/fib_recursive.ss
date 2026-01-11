(define (fib n)
  (if (<= n 1)
      n
      (+ (fib (- n 1))
         (fib (- n 2)))))

(let ((n (if (> (length (command-line-arguments)) 0)
             (string->number (car (command-line-arguments)))
             0)))
  (display (fib n))
  (newline))
