LLVM Custom Optimization Pass
Overview

This project implements a custom LLVM function pass named hello-world.

The pass performs the following transformations:

Constant Propagation

Instruction Combining

Dead Code Elimination

Strength Reduction

Common Subexpression Elimination

The pass is implemented in HelloWorld.cpp and loaded dynamically as an LLVM pass plugin.

Prerequisites

The project uses LLVM 24.0.0git.

Verify the LLVM version with:

/home/mcw/llvm/llvm-project/build/bin/llvm-config --version


Expected output:

24.0.0git


The LLVM installation used for this project is:

/home/mcw/llvm/llvm-project/


The LLVM binaries are located in:

/home/mcw/llvm/llvm-project/build/bin/

Project Files

The project contains the following important files:

HelloWorld.cpp
test.c
llvm/Transforms/Utils/HelloWorld.h


The generated files are:

HelloWorldPass.so
test.ll
optimized.ll

Step 1: Verify the LLVM Installation

Run:

/home/mcw/llvm/llvm-project/build/bin/llvm-config --version


Then verify clang:

/home/mcw/llvm/llvm-project/build/bin/clang --version


Verify opt:

/home/mcw/llvm/llvm-project/build/bin/opt --version


All LLVM tools should come from the same LLVM build.

Step 2: Build the LLVM Pass

Navigate to the directory containing HelloWorld.cpp.

Compile the pass as a shared library:

/home/mcw/llvm/llvm-project/build/bin/clang++ \
  -fPIC \
  -shared \
  -std=c++17 \
  HelloWorld.cpp \
  -I/home/mcw/llvm/llvm-project/llvm/include \
  -I/home/mcw/llvm/llvm-project/build/include \
  -o HelloWorldPass.so


After successful compilation, verify that the plugin exists:

ls -lh HelloWorldPass.so


The generated shared library is:

HelloWorldPass.so

Step 3: Compile the C Test Program to LLVM IR

The test program is stored in:

test.c


Generate LLVM IR using:

/home/mcw/llvm/llvm-project/build/bin/clang \
  -S \
  -emit-llvm \
  -O0 \
  test.c \
  -o test.ll


This generates:

test.ll


The -O0 option is used to prevent Clang's normal optimization pipeline from performing the transformations before the custom pass is executed.

Step 4: Run the Custom LLVM Pass

Run the custom pass using LLVM's opt tool:

/home/mcw/llvm/llvm-project/build/bin/opt \
  -load-pass-plugin=./HelloWorldPass.so \
  -passes=hello-world \
  test.ll \
  -S \
  -o optimized.ll


This command performs the following operations:

Loads HelloWorldPass.so

Registers and runs the hello-world pass

Reads test.ll

Applies the custom transformations

Writes the transformed LLVM IR to optimized.ll

Step 5: Compare the LLVM IR

Compare the input and output LLVM IR using:

diff -u test.ll optimized.ll


The original LLVM IR is stored in:

test.ll


The transformed LLVM IR is stored in:

optimized.ll


The diff command displays the changes made by the custom optimization pass.

Step 6: Inspect the LLVM IR

To inspect the original IR:

cat test.ll


To inspect the transformed IR:

cat optimized.ll


Arithmetic instructions can be searched using:

grep -E "add|sub|mul|sdiv|udiv|shl|ashr|lshr" test.ll


and:

grep -E "add|sub|mul|sdiv|udiv|shl|ashr|lshr" optimized.ll

Step 7: Verify the Optimized Program

The optimized LLVM IR can be compiled into an executable using the C compiler:

/home/mcw/llvm/llvm-project/build/bin/clang \
  optimized.ll \
  -o optimized_test


Run the resulting executable:

./optimized_test


The output can be compared with the output of the original C program.

Complete Reproduction

The complete sequence from source code to optimized LLVM IR is:

Build the Pass
/home/mcw/llvm/llvm-project/build/bin/clang++ \
  -fPIC \
  -shared \
  -std=c++17 \
  HelloWorld.cpp \
  -I/home/mcw/llvm/llvm-project/llvm/include \
  -I/home/mcw/llvm/llvm-project/build/include \
  -o HelloWorldPass.so

Generate LLVM IR
/home/mcw/llvm/llvm-project/build/bin/clang \
  -S \
  -emit-llvm \
  -O0 \
  test.c \
  -o test.ll

Run the Custom Pass
/home/mcw/llvm/llvm-project/build/bin/opt \
  -load-pass-plugin=./HelloWorldPass.so \
  -passes=hello-world \
  test.ll \
  -S \
  -o optimized.ll

Compare the IR
diff -u test.ll optimized.ll

Compile the Optimized IR
/home/mcw/llvm/llvm-project/build/bin/clang \
  optimized.ll \
  -o optimized_test

Run the Optimized Program
./optimized_test

Optimization Passes
Constant Propagation

The pass evaluates instructions whose operands are compile-time constants and replaces the instruction with the resulting constant.

Instruction Combining

The pass simplifies arithmetic operations using algebraic identities and constant folding.

The pass handles transformations such as:

x + 0
x - 0
x * 1
x * 0
x / 1
x & 0
x | 0
x ^ 0

Dead Code Elimination

Instructions that produce unused values and have no observable side effects are removed from the function.

Strength Reduction

Multiplication by a positive power of two can be converted into a left shift.

For example:

x * 2


can be represented as:

x << 1


Similarly:

x * 4


can be represented as:

x << 2


and:

x * 8


can be represented as:

x << 3

Common Subexpression Elimination

Repeated equivalent expressions within the same basic block are detected and redundant computations are replaced with an existing result.

For example:

x = a + b
y = a + b


can reuse the result of the first a + b computation.

Troubleshooting
Unknown Pass Name

If opt reports:

unknown pass name 'hello-world'


make sure the plugin is loaded using:

-load-pass-plugin=./HelloWorldPass.so


The complete command is:

/home/mcw/llvm/llvm-project/build/bin/opt \
  -load-pass-plugin=./HelloWorldPass.so \
  -passes=hello-world \
  test.ll \
  -S \
  -o optimized.ll

Plugin Loading Error

Make sure that HelloWorldPass.so was built using the same LLVM installation that provides opt.

Check:

/home/mcw/llvm/llvm-project/build/bin/llvm-config --version


and:

/home/mcw/llvm/llvm-project/build/bin/opt --version


Both should correspond to the same LLVM build.

Output Files

After completing the steps, the directory contains:

HelloWorld.cpp
HelloWorldPass.so
test.c
test.ll
optimized.ll
llvm/
└── Transforms/
    └── Utils/
        └── HelloWorld.h


The main files used for verification are:

test.ll
optimized.ll


The difference between these files shows the transformations performed by the custom LLVM pass.
