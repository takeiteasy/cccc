(define (reverse-acc xs acc)
  (cond ((eq xs nil) acc)
        (t (reverse-acc (cdr xs) (cons (car xs) acc)))))
