(define args (command-line))
(define n (if (> (length args) 1) (string->number (list-ref args 1)) 0))

(define (map-hit-miss n)
  (let ((m (make-eqv-hashtable)))
    (let loop ((i 0))
      (when (< i n)
        (hashtable-set! m i (+ i 1))
        (loop (+ i 1))))
    (let loop ((i 0) (sum 0))
      (if (>= i n)
          sum
          (let* ((k (if (even? i) i (+ i n)))
                 (v (hashtable-ref m k 1)))
            (loop (+ i 1) (+ sum v)))))))

(display (map-hit-miss n))
(newline)
