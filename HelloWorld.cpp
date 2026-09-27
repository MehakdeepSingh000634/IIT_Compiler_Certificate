#include "llvm/Transforms/Utils/HelloWorld.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/Local.h"

#include <map>
#include <string>
#include <utility>

using namespace llvm;

namespace {

/// -------------------------------------------------------------------------
/// Constant propagation / folding
/// -------------------------------------------------------------------------
static bool propagateConstants(Function &Fn) {
  bool Modified = false;
  const DataLayout &Layout = Fn.getDataLayout();

  for (BasicBlock &Block : Fn) {
    for (Instruction &Inst : make_early_inc_range(Block)) {
      if (Inst.isTerminator() || Inst.mayHaveSideEffects())
        continue;

      Constant *Result = ConstantFoldInstruction(&Inst, Layout);

      if (Result == nullptr)
        continue;

      if (Result == &Inst)
        continue;

      Inst.replaceAllUsesWith(Result);
      Inst.eraseFromParent();
      Modified = true;
    }
  }

  return Modified;
}

/// -------------------------------------------------------------------------
/// Utility routines for instruction simplification
/// -------------------------------------------------------------------------
static Value *simplifyIntegerInstruction(BinaryOperator &Op) {
  Value *A = Op.getOperand(0);
  Value *B = Op.getOperand(1);
  Type *ResultTy = Op.getType();

  if (!ResultTy->isIntegerTy())
    return nullptr;

  // x op x simplifications.
  if (A == B) {
    switch (Op.getOpcode()) {
    case Instruction::Xor:
    case Instruction::Sub:
      return ConstantInt::get(ResultTy, 0);

    case Instruction::And:
    case Instruction::Or:
      return A;

    case Instruction::UDiv:
    case Instruction::SDiv:
      return ConstantInt::get(ResultTy, 1);

    default:
      break;
    }
  }

  Constant *CA = dyn_cast<Constant>(A);
  Constant *CB = dyn_cast<Constant>(B);

  // Put a constant on the right for commutative operations.
  if (CA && !CB && Op.isCommutative()) {
    std::swap(A, B);
    std::swap(CA, CB);
  }

  if (!CB)
    return nullptr;

  switch (Op.getOpcode()) {
  case Instruction::Add:
    if (CB->isNullValue())
      return A;
    break;

  case Instruction::Sub:
    if (CB->isNullValue())
      return A;
    break;

  case Instruction::Mul:
    if (CB->isNullValue())
      return ConstantInt::get(ResultTy, 0);

    if (CB->isOneValue())
      return A;
    break;

  case Instruction::UDiv:
  case Instruction::SDiv:
    if (CB->isOneValue())
      return A;
    break;

  case Instruction::And:
    if (CB->isNullValue())
      return ConstantInt::get(ResultTy, 0);
    break;

  case Instruction::Or:
  case Instruction::Xor:
    if (CB->isNullValue())
      return A;
    break;

  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    if (CB->isNullValue())
      return A;
    break;

  default:
    break;
  }

  return nullptr;
}

/// -------------------------------------------------------------------------
/// 1 + 2. Constant propagation and instruction combining
/// -------------------------------------------------------------------------
static bool combineInstructions(Function &Fn) {
  bool Modified = false;

  for (BasicBlock &Block : Fn) {
    for (Instruction &Inst : make_early_inc_range(Block)) {
      auto *Binary = dyn_cast<BinaryOperator>(&Inst);
      if (!Binary)
        continue;

      if (Value *Replacement = simplifyIntegerInstruction(*Binary)) {
        Binary->replaceAllUsesWith(Replacement);
        Binary->eraseFromParent();
        Modified = true;
        continue;
      }

      // Constant folding of a binary instruction.
      if (Binary->getType()->isIntegerTy()) {
        Constant *L = dyn_cast<Constant>(Binary->getOperand(0));
        Constant *R = dyn_cast<Constant>(Binary->getOperand(1));

        if (L && R) {
          const DataLayout &DL = Fn.getDataLayout();
          if (Constant *Folded = ConstantFoldInstruction(Binary, DL)) {
            Binary->replaceAllUsesWith(Folded);
            Binary->eraseFromParent();
            Modified = true;
          }
        }
      }
    }
  }

  return Modified;
}

/// -------------------------------------------------------------------------
/// Create x << log2(C) for multiplication by a positive power of two.
/// This handles the requested "power of 2 -> shift" optimization.
/// -------------------------------------------------------------------------
static Instruction *makeMultiplyShift(BinaryOperator &Mul) {
  if (Mul.getOpcode() != Instruction::Mul)
    return nullptr;

  Value *Variable = Mul.getOperand(0);
  auto *ConstantOperand =
      dyn_cast<ConstantInt>(Mul.getOperand(1));

  // Accept C * x as well as x * C.
  if (!ConstantOperand) {
    ConstantOperand = dyn_cast<ConstantInt>(Mul.getOperand(0));
    if (!ConstantOperand)
      return nullptr;

    Variable = Mul.getOperand(1);
  }

  const APInt &Value = ConstantOperand->getValue();

  // Only positive powers of two are transformed.
  if (Value.isZero() || !Value.isPowerOf2())
    return nullptr;

  unsigned ShiftAmount = Value.logBase2();

  if (ShiftAmount == 0)
    return nullptr;

  Constant *ShiftValue =
      ConstantInt::get(Variable->getType(), ShiftAmount);

  return BinaryOperator::Create(
      Instruction::Shl,
      Variable,
      ShiftValue,
      "",
      &Mul);
}

/// -------------------------------------------------------------------------
/// 4. Strength reduction
/// -------------------------------------------------------------------------
static bool reduceStrength(Function &Fn) {
  bool Modified = false;

  for (BasicBlock &Block : Fn) {
    for (Instruction &Inst : make_early_inc_range(Block)) {
      auto *Mul = dyn_cast<BinaryOperator>(&Inst);
      if (!Mul)
        continue;

      Instruction *Replacement = makeMultiplyShift(*Mul);

      if (!Replacement)
        continue;

      Mul->replaceAllUsesWith(Replacement);
      Mul->eraseFromParent();

      Modified = true;
    }
  }

  return Modified;
}

/// -------------------------------------------------------------------------
/// 3. Dead code elimination
/// -------------------------------------------------------------------------
static bool removeUnusedInstructions(Function &Fn) {
  bool Modified = false;

  for (BasicBlock &Block : Fn) {
    SmallVector<Instruction *, 32> Candidates;

    for (Instruction &Inst : Block)
      Candidates.push_back(&Inst);

    while (!Candidates.empty()) {
      Instruction *Inst = Candidates.pop_back_val();

      if (!isInstructionTriviallyDead(Inst))
        continue;

      SmallVector<Instruction *, 8> Dependencies;

      for (Value *Operand : Inst->operands()) {
        if (auto *Dependency = dyn_cast<Instruction>(Operand))
          Dependencies.push_back(Dependency);
      }

      Inst->eraseFromParent();
      Modified = true;

      for (Instruction *Dependency : Dependencies)
        Candidates.push_back(Dependency);
    }
  }

  return Modified;
}

/// -------------------------------------------------------------------------
/// CSE support
/// -------------------------------------------------------------------------
static bool canEliminate(Instruction &Inst) {
  if (Inst.isTerminator())
    return false;

  if (Inst.mayHaveSideEffects())
    return false;

  // Do not eliminate instructions whose result depends on memory.
  if (Inst.mayReadFromMemory())
    return false;

  return isa<BinaryOperator>(&Inst) ||
         isa<CmpInst>(&Inst) ||
         isa<CastInst>(&Inst) ||
         isa<SelectInst>(&Inst) ||
         isa<GetElementPtrInst>(&Inst);
}


static std::string expressionSignature(Instruction &Inst) {
  SmallVector<const Value *, 4> Arguments;

  for (Value *Operand : Inst.operands())
    Arguments.push_back(Operand);

  if (Inst.isCommutative() && Arguments.size() == 2) {
    if (Arguments[1] < Arguments[0])
      std::swap(Arguments[0], Arguments[1]);
  }

  std::string Signature;
  raw_string_ostream Stream(Signature);

  Stream << Inst.getOpcode();
  Stream << '|';
  Stream << *Inst.getType();

  for (const Value *Argument : Arguments) {
    Stream << '|';
    Stream << Argument;
  }

  if (auto *Compare = dyn_cast<CmpInst>(&Inst)) {
    Stream << "|cmp=" << Compare->getPredicate();
  }

  return Stream.str();
}

/// -------------------------------------------------------------------------
/// 5. Common subexpression elimination
/// -------------------------------------------------------------------------
static bool eliminateRepeatedExpressions(Function &Fn) {
  bool Modified = false;

  for (BasicBlock &Block : Fn) {
    std::map<std::string, Instruction *> Available;

    for (Instruction &Inst : make_early_inc_range(Block)) {
      if (!canEliminate(Inst))
        continue;

      std::string Signature = expressionSignature(Inst);

      auto Existing = Available.find(Signature);

      if (Existing == Available.end()) {
        Available.emplace(Signature, &Inst);
        continue;
      }

      Instruction *Previous = Existing->second;

      Inst.replaceAllUsesWith(Previous);
      Inst.eraseFromParent();

      Modified = true;
    }
  }

  return Modified;
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Pass driver
//===----------------------------------------------------------------------===//

PreservedAnalyses
HelloWorldPass::run(Function &F, FunctionAnalysisManager &AM) {
  (void)AM;

  bool Changed = false;

  for (unsigned Iteration = 0; Iteration < 6; ++Iteration) {
    bool LocalChange = false;

    // First expose constants and simple algebraic identities.
    LocalChange |= propagateConstants(F);
    LocalChange |= combineInstructions(F);

    // Replace multiplication by powers of two with shifts.
    LocalChange |= reduceStrength(F);

    // Remove instructions that have become unused.
    LocalChange |= removeUnusedInstructions(F);

    // Finally merge identical expressions within each basic block.
    LocalChange |= eliminateRepeatedExpressions(F);

    if (!LocalChange)
      break;

    Changed = true;
  }

  return Changed ? PreservedAnalyses::none()
                 : PreservedAnalyses::all();
}
