// #1287 regression fixture: a bare statement fragment, spliced textually
// inside the enclosing function body via #include -- not a header, not a
// standalone translation unit (`acc`/`n` below resolve only because of
// where this file is #include'd from). See
// tests/test_serialize_infunc_include_1287.c.
acc += n;
acc += n * 2;
