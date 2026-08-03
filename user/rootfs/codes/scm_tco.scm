; TCO 演示: 尾递归 100 万次求和, 不爆栈
(define (sum-to n acc) (if (= n 0) acc (sum-to (- n 1) (+ acc n))))
(display "sum 1..1000000 = ")
(display (sum-to 1000000 0))
(display "
")
