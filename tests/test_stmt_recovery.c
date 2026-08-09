// EXPECT_COMPILE_ERROR
// Test statement-level error recovery
// This file has 4 stray statement errors
// All should be reported with Level 2 recovery

int main() {
    int x = 1;

    // Error 1: Stray break
    break;

    int y = 2;

    // Error 2: Stray continue
    continue;

    int z = 3;

    // Error 3: Stray case
    case 42:
        x = 10;

    // Error 4: Stray default
    default:
        y = 20;

    int result = x + y + z;
    // x=10 (from case), y=20 (from default), z=3 => 33
    if (result != 33) return 1;  // Assert result == 33
    return 42;
}
