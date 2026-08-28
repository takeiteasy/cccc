(define (append x y)
  (cond ((eq x nil) y)
        (t (cons (car x) (append (cdr x) y)))))

;; A quoted list literal -- lowers to a nested CONS chain built once, at
;; comptime, then reused across calls (see ccccl_lower_quote in
;; ccccl_lower.h and ccccl_sym_N()'s memoized-cache pattern).
(define (letters) (quote (e f g)))
