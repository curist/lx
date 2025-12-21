(define args (command-line))
(define n (if (> (length args) 1) (string->number (list-ref args 1)) 0))

(define (array-fill n)
  (let ((arr (make-vector n 0)))
    (let loop ((i 0))
      (when (< i n)
        (vector-set! arr i i)
        (loop (+ i 1))))
    (let loop ((i 0) (sum 0))
      (if (>= i n)
          sum
          (let ((v (+ (vector-ref arr i) 1)))
            (vector-set! arr i v)
            (loop (+ i 1) (+ sum v)))))))

(display (array-fill n))
(newline)
