(define (make-adder n) (lambda (x) (+ x n)))

(define (square x) (* x x))

;; `square` here is a bare toplevel name in value position, not a call --
;; exercises the __thunk wrapper (see ccccl_emit_thunk in
;; src/ccccl_comptime.c) that lets ccccl_apply call a toplevel define, which
;; has no (captures, args) entry point of its own.
(define (get-square) square)
