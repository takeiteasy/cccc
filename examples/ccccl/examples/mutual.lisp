(define (evenp x) (cond ((eq x nil) t) (t (oddp (cdr x)))))
(define (oddp x) (cond ((eq x nil) nil) (t (evenp (cdr x)))))
