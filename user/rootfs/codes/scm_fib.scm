; fib + cond + let 综合
(define (fib n)
  (cond ((< n 2) n)
        (else (+ (fib (- n 1)) (fib (- n 2))))))

(define (grade s)
  (cond ((>= s 90) 'A) ((>= s 80) 'B) ((>= s 60) 'C) (else 'F)))

(display "fib(20) = ")
(display (fib 20))
(display "
")
(display "grade(85) = ")
(display (grade 85))
(display "
")
(display "let: ")
(display (let ((x 5) (y 3)) (* x y)))
(display "
")
