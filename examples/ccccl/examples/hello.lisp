;; hello.lisp -- "program" mode: toplevel executable forms, no hand-written
;; host TU. `cccc -c=native src/ccccl_comptime.c runtime/ccccl_rt.c ...`
;; reads this, lowers it, and links a self-contained binary that synthesizes
;; its own `main()`.
;;
;; The executable forms below run before `fact` is *textually* defined --
;; the two-pass declare-then-lower hoists every `define`, so file order
;; among the executable forms is all that matters.

(let ((n 10))                 ; non-final toplevel LET, exercises the
  (print (fact n)))           ; emit_stmt scratch-dest path

(print (quote (done)))

(define (fact n)
  (if (< n 2)
      1
      (* n (fact (- n 1)))))
