// Fixture for tests/test_serialize_header_static_skip.c (#999).
//
// Deliberately has NO #pragma once / #ifndef include guard -- this test is
// about a `static` function definition reached by more than one
// translation unit through an ordinary #include, independent of the
// separate (out-of-scope, tracked on its own ticket) bug where
// pragma_once/include_guard state persists across TUs that share one
// cccc invocation. A guarded header would confound the two.
static int header_static_skip_999_helper(int x) { return x + 1; }
