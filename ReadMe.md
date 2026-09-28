LLVM HelloWorld Optimization Pass

This project implements a custom LLVM function pass containing the following transformations:

Constant propagation and constant folding

Instruction combining

Strength reduction

Dead code elimination

Common subexpression elimination

The pass is implemented in HelloWorld.cpp and is loaded dynamically into LLVM's opt tool.

Project Structure

The directory should contain:

.
├── HelloWorld.cpp
├── test.cpp
├── HelloWorldPass.so
├── test.ll
├── optimized.ll
└── llvm/
    └── Transforms/
        └── Utils/
            └── HelloWorld.h


HelloWorldPass.so, test.ll, and optimized.ll are generated during the build and execution process.

LLVM Version

This project was tested with:

LLVM 24.0.0git


The LLVM source/build directory used in this setup is:

/home/mcw/llvm/llvm-project/


The LLVM binaries are located at:

/home/mcw/llvm/llvm-project/build/bin/

1. Verify LLVM Installation

Check the LLVM version:

/home/mcw/llvm/llvm-project/build/bin/llvm-config --version


Expected output:

24.0.0git


Check clang:

/home/mcw/llvm/llvm-project/build/bin/clang++ --version


Check opt:

/home/mcw/llvm/llvm-project/build/bin/opt --version


All three tools should belong to the same LLVM build.

2. Create the Header File

Create the required directory:

mkdir -p llvm/Transforms/Utils


Create:

llvm/Transforms/Utils/HelloWorld.h


The header should contain:

#ifndef LLVM_TRANSFORMS_UTILS_HELLOWORLD_H
#define LLVM_TRANSFORMS_UTILS_HELLOWORLD_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class HelloWorldPass : public detail::PassInfoMixin<HelloWorldPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif

3. Build the LLVM Pass

Make sure the terminal is in the directory containing HelloWorld.cpp.

Remove an old plugin if one exists:

rm -f HelloWorldPass.so


Build the pass:

/home/mcw/llvm/llvm-project/build/bin/clang++ \
  -fPIC \
  -shared \
  -std=c++17 \
  HelloWorld.cpp \
  -I/home/mcw/llvm/llvm-project/llvm/include \
  -I/home/mcw/llvm/llvm-project/build/include \
  -o HelloWorldPass.so


The command should complete without errors.

Verify that the shared library was created:

ls -lh HelloWorldPass.so

4. Compile the Test Program to LLVM IR

The test program is stored in:

test.cpp


Generate LLVM IR using clang++:

/home/mcw/llvm/llvm-project/build/bin/clang++ \
  -S \
  -emit-llvm \
  -O0 \
  test.cpp \
  -o test.ll


This produces:

test.ll


The -O0 option is used so that LLVM's built-in optimization passes do not perform the transformations before the custom pass gets a chance to process the IR.

5. Run the Custom Pass

Load the generated shared library into opt:

/home/mcw/llvm/llvm-project/build/bin/opt \
  -load-pass-plugin=./HelloWorldPass.so \
  -passes=hello-world \
  test.ll \
  -S \
  -o optimized.ll


The output is:

optimized.ll


The important parts of the command are:

-load-pass-plugin=./HelloWorldPass.so


which loads the custom pass, and:

-passes=hello-world


which invokes the pass registered by HelloWorldPass.

6. Compare the Input and Output IR

Use:

diff -u test.ll optimized.ll


This displays the differences between the original LLVM IR and the IR after the custom pass.

A difference in the ModuleID alone is not an optimization transformation. For example:

-; ModuleID = 'test.c'
+; ModuleID = 'test.ll'


does not indicate that the pass changed the program.

Look for changes to instructions such as:

add
sub
mul
sdiv
udiv
shl
ashr
lshr

7. Inspect Arithmetic Instructions

To inspect arithmetic instructions in the original IR:

grep -E "add|sub|mul|sdiv|udiv|shl|ashr|lshr" test.ll


To inspect arithmetic instructions after optimization:

grep -E "add|sub|mul|sdiv|udiv|shl|ashr|lshr" optimized.ll


This is particularly useful for checking strength reduction.

For example, multiplication by a power of two can be transformed from:

mul


into:

shl

8. Verify Strength Reduction

The pass recognizes integer multiplication by a positive power of two.

For example, an expression equivalent to:

x * 2


can be represented using:

x << 1


Similarly:

x * 4


can become:

x << 2


and:

x * 8


can become:

x << 3


Search the optimized IR with:

grep "shl" optimized.ll

9. Verify Constant Folding

Search the original IR:

grep "add" test.ll


and compare it with:

grep "add" optimized.ll


Constant expressions that are available directly as LLVM constants can be folded by the constant propagation/folding portion of the pass.

10. Verify Dead Code Elimination

Search the original IR for instructions associated with values that are never used:

grep -E "mul|add|sub" test.ll


Then compare:

grep -E "mul|add|sub" optimized.ll


Instructions that produce unused values and have no side effects can be removed by the dead code elimination portion of the pass.

11. Verify Common Subexpression Elimination

CSE is performed within individual basic blocks.

For example, if the IR contains equivalent computations:

%1 = add i32 %a, %b
%2 = add i32 %a, %b


the second computation can be replaced with the result of the first.

Inspect the relevant portions of:

cat test.ll


and:

cat optimized.ll


or use:

diff -u test.ll optimized.ll

12. Run the Optimized Program

After generating optimized.ll, compile it:

/home/mcw/llvm/llvm-project/build/bin/clang++ \
  optimized.ll \
  -o optimized_test


Run it:

./optimized_test


The program output should remain semantically equivalent to the original program.

13. Complete Reproduction Sequence

After the source files are already present, the complete sequence is:

rm -f HelloWorldPass.so test.ll optimized.ll optimized_test


Build the pass:

/home/mcw/llvm/llvm-project/build/bin/clang++ \
  -fPIC \
  -shared \
  -std=c++17 \
  HelloWorld.cpp \
  -I/home/mcw/llvm/llvm-project/llvm/include \
  -I/home/mcw/llvm/llvm-project/build/include \
  -o HelloWorldPass.so


Generate LLVM IR:

/home/mcw/llvm/llvm-project/build/bin/clang++ \
  -S \
  -emit-llvm \
  -O0 \
  test.cpp \
  -o test.ll


Run the custom pass:

/home/mcw/llvm/llvm-project/build/bin/opt \
  -load-pass-plugin=./HelloWorldPass.so \
  -passes=hello-world \
  test.ll \
  -S \
  -o optimized.ll


Compare the IR:

diff -u test.ll optimized.ll


Compile the optimized IR:

/home/mcw/llvm/llvm-project/build/bin/clang++ \
  optimized.ll \
  -o optimized_test


Run the program:

./optimized_test

14. Troubleshooting
unknown pass name 'hello-world'

Make sure the plugin is loaded:

-load-pass-plugin=./HelloWorldPass.so


The following command is incorrect because it does not load the plugin:

opt -passes=hello-world test.ll


Use:

/home/mcw/llvm/llvm-project/build/bin/opt \
  -load-pass-plugin=./HelloWorldPass.so \
  -passes=hello-world \
  test.ll \
  -S \
  -o optimized.ll

Option 'phicse-debug-hash' registered more than once

This can happen when the pass plugin is built by linking another copy of the LLVM libraries into the shared object.

Build the plugin without:

--libs all


and without:

--ldflags


Use:

/home/mcw/llvm/llvm-project/build/bin/clang++ \
  -fPIC \
  -shared \
  -std=c++17 \
  HelloWorld.cpp \
  -I/home/mcw/llvm/llvm-project/llvm/include \
  -I/home/mcw/llvm/llvm-project/build/include \
  -o HelloWorldPass.so

Only ModuleID changes in diff

For example:

-; ModuleID = 'test.c'
+; ModuleID = 'test.ll'


This does not demonstrate that an optimization occurred.

Inspect the IR:

cat test.ll


and:

cat optimized.ll


Also search for arithmetic instructions:

grep -E "add|sub|mul|sdiv|udiv|shl|ashr|lshr" test.ll


and:

grep -E "add|sub|mul|sdiv|udiv|shl|ashr|lshr" optimized.ll

15. Important Build Consistency

Use the same LLVM installation for all LLVM tools.

In this setup:

/home/mcw/llvm/llvm-project/build/bin/clang++
/home/mcw/llvm/llvm-project/build/bin/opt
/home/mcw/llvm/llvm-project/build/bin/llvm-config


Avoid mixing these with system LLVM installations such as:

/usr/bin/clang++
/usr/bin/opt
/usr/bin/llvm-config


because different LLVM versions can cause plugin loading and ABI problems.


One thing I'd recommend before submitting this: **don't claim that all five passes are successfully demonstrated just because the pass loads**. Your current `diff` showed only a `ModuleID` change, so the README correctly distinguishes between *running the pass* and *observing an actual transformation*.
