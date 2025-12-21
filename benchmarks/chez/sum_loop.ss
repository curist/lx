(define args (command-line))
(define n (if (> (length args) 1) (string->number (list-ref args 1)) 0))

(define (sum-loop n)
  (let loop ((i 1) (acc 0))
    (if (> i n)
        acc
        (loop (+ i 1) (+ acc i)))))

(display (sum-loop n))
(newline)
