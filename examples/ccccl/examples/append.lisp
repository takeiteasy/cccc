(define (append x y)
  (cond ((eq x nil) y)
        (t (cons (car x) (append (cdr x) y)))))
