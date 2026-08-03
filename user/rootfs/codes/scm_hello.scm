; RmikuScheme 示例: Hello + 闭包
(define (greet name) (display "Hello, ") (display name) (display "!
"))
(greet "RmikuScheme")

; 闭包: 加法器
(define (adder x) (lambda (y) (+ x y)))
(define add5 (adder 5))
(display (add5 3))
(display "
")
