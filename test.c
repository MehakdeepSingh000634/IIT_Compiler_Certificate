#include <stdio.h>

int test_constants() {
    int a = 10;
    int b = 20;
    int c = a + b;
    int d = c * 2;
    int e = d - 5;

    printf("Constant propagation: %d\n", e);
    return e;
}

int test_inst_combine(int x) {
    int a = x + 0;
    int b = a * 1;
    int c = b - 0;
    int constant = 4 + 6;
    int result = c + constant;

    printf("Instruction combining: %d\n", result);
    return result;
}

int test_dead_code(int x) {
    int unused1 = x * 7;
    int unused2 = unused1 + 100;
    int useful = x + 5;

    printf("Dead code elimination: %d\n", useful);
    return useful;
}

int test_strength_reduction(int x) {
    int a = x * 2;
    int b = x * 4;
    int c = x * 8;
    int result = a + b + c;

    printf("Strength reduction: %d\n", result);
    return result;
}

int test_cse(int a, int b) {
    int first = a + b;
    int second = b + a;
    int result = first * second;

    printf("Common subexpression elimination: %d\n", result);
    return result;
}

int test_combined() {
    int a = 2;
    int b = 3;
    int c = a + b;
    int d = c + 0;
    int e = d * 1;
    int f = e * 8;
    int g = f + c;
    int h = c + f;
    int unused = g * 100;
    int result = g + h;

    printf("Combined optimization: %d\n", result);
    return result;
}

int main() {
    printf("===== LLVM Optimization Test =====\n");

    test_constants();
    test_inst_combine(10);
    test_dead_code(10);
    test_strength_reduction(5);
    test_cse(6, 4);
    test_combined();

    printf("===== Tests completed =====\n");

    return 0;
}
