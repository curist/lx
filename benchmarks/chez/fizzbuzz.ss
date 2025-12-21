(define args (command-line))
(define n (if (> (length args) 1) (string->number (list-ref args 1)) 0))

(define (fizzbuzz n)
  (let loop ((i 1) (acc 0))
    (if (> i n)
        acc
        (let ((a (= (modulo i 3) 0))
              (b (= (modulo i 5) 0)))
          (cond
            ((and a b) (loop (+ i 1) (+ acc 3)))
            (a (loop (+ i 1) (+ acc 1)))
            (b (loop (+ i 1) (+ acc 2)))
            (else (loop (+ i 1) acc)))))))

(display (fizzbuzz n))
(newline)
