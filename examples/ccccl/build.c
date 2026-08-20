// build.c — build script for the ccccl example, run with: cccc --build build.c
//
// This is a trimmed copy of ccccl's own build.c, kept here as a runnable
// example rather than a full project checkout — see README.md. The
// host-test and golden-output verification targets (which depended on
// tests/ and tools/, not copied here) are omitted; only the parts that
// build and run the three example programs remain.
//
// Targets:
//   ccccl_rt      static library from runtime/ccccl_rt.c
//   example_gen   invokes cccc -c=generated on src/ccccl_comptime.c, once
//                 per example, producing build/append.gen.c,
//                 build/lambda_head.gen.c, and build/mutual.gen.c
//   example       links build/append.gen.c + examples/append_main.c against
//                 ccccl_rt with plain cc -- the proof that the compiler's
//                 output needs no cccc to build or run. Exercises
//                 self-recursion and COND.
//   example_lambda  links build/lambda_head.gen.c + examples/lambda_head_main.c
//                 the same way. Exercises LAMBDA and closure application
//                 (ccccl_closure/ccccl_apply), which `example` never touches.
//   example_mutual  links build/mutual.gen.c + examples/mutual_main.c the
//                 same way. Exercises true mutual recursion between two
//                 independently-defined toplevel defines (evenp/oddp),
//                 which neither `example` nor `example_lambda` touches.
//   check         all three examples (the default)

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *rt;
    BuildTarget *example_gen, *example, *example_lambda, *example_mutual,
        *check;

    rt = StaticLib(ctx, "ccccl_rt");
    AddSource(rt, "runtime/ccccl_rt.c");
    AddInclude(rt, "runtime");

    // The comptime pass: src/ccccl_comptime.c is the only file ever passed
    // to cccc (see its file comment for why). Runs once per example --
    // each invocation selects its .lisp source via -DCCCCL_LISP_PATH -- as
    // a single `;`-sequenced RunCustom command.
    example_gen =
        RunCustom(ctx, "example_gen",
                  "cccc -c=generated --emit-only src/ccccl_comptime.c "
                  "-Iinclude/ccccl -Iruntime "
                  "-DCCCCL_LISP_PATH='\"examples/append.lisp\"' "
                  "-o build/append.gen.c ; "
                  "cccc -c=generated --emit-only src/ccccl_comptime.c "
                  "-Iinclude/ccccl -Iruntime "
                  "-DCCCCL_LISP_PATH='\"examples/lambda_head.lisp\"' "
                  "-o build/lambda_head.gen.c ; "
                  "cccc -c=generated --emit-only src/ccccl_comptime.c "
                  "-Iinclude/ccccl -Iruntime "
                  "-DCCCCL_LISP_PATH='\"examples/mutual.lisp\"' "
                  "-o build/mutual.gen.c");
    DeclareOutput(example_gen, "build/append.gen.c");
    DeclareOutput(example_gen, "build/lambda_head.gen.c");
    DeclareOutput(example_gen, "build/mutual.gen.c");
    AddInput(example_gen, "examples/append.lisp");
    AddInput(example_gen, "examples/lambda_head.lisp");
    AddInput(example_gen, "examples/mutual.lisp");
    AddInput(example_gen, "src/ccccl_comptime.c");
    AddInput(example_gen, "include/ccccl/ccccl_plan.h");
    AddInput(example_gen, "include/ccccl/ccccl_reader.h");
    AddInput(example_gen, "include/ccccl/ccccl_lower.h");
    AddInput(example_gen, "runtime/ccccl_rt.h");

    // build/*.gen.c does not exist at graph-declaration time on a clean
    // checkout -- AddSourcesGlobDeferred defers each glob to build time,
    // once example_gen has run.
    example = Executable(ctx, "append");
    AddSource(example, "examples/append_main.c");
    AddSourcesGlobDeferred(example, "build/append.gen.c");
    AddInclude(example, "runtime");
    LinkWith(example, rt);
    DependsOn(example, example_gen);

    example_lambda = Executable(ctx, "lambda_head");
    AddSource(example_lambda, "examples/lambda_head_main.c");
    AddSourcesGlobDeferred(example_lambda, "build/lambda_head.gen.c");
    AddInclude(example_lambda, "runtime");
    LinkWith(example_lambda, rt);
    DependsOn(example_lambda, example_gen);

    example_mutual = Executable(ctx, "mutual");
    AddSource(example_mutual, "examples/mutual_main.c");
    AddSourcesGlobDeferred(example_mutual, "build/mutual.gen.c");
    AddInclude(example_mutual, "runtime");
    LinkWith(example_mutual, rt);
    DependsOn(example_mutual, example_gen);

    check = RunCustom(ctx, "check", "true");
    DependsOn(check, example);
    DependsOn(check, example_lambda);
    DependsOn(check, example_mutual);

    return BuildDefault(ctx);
}
