// Tests for [[cccc::test(return_epsilon = N)]] — configurable float tolerance
// (ticket #351).
// CCCC_FLAGS: --testing

#pragma cccc suite begin "return_epsilon"

// Custom epsilon: 3.141595 is within 1e-5 of 3.14159.
[[cccc::test(return = 3.14159, return_epsilon = 1e-5)]]
double test_approx_pi(void) { return 3.141595; }

// Loose epsilon: 0.0005 is within 1e-3 of 0.0.
[[cccc::test(return = 0.0, return_epsilon = 1e-3)]]
double test_zero_loose(void) { return 0.0005; }

// Large epsilon: works with != too.
[[cccc::test(return != 99.0, return_epsilon = 0.5)]]
double test_ne_with_epsilon(void) { return 1.0; }

// Default epsilon (1e-9) still applies when return_epsilon is omitted.
[[cccc::test(return = 1.0)]]
double test_exact_one(void) { return 1.0; }

#pragma cccc suite end
