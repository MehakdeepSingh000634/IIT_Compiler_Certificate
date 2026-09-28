
#include "llvm/Transforms/Utils/HelloWorld.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"

#include <map>
#include <string>
#include <utility>

using namespace llvm;

namespace {

//===----------------------------------------------------------------------===//
// 1. Constant propagation
//===----------------------------------------------------------------------===//

static bool propagateConstants(Function &Fn) {
  bool Changed = false;
  const DataLayout &DL = Fn.getDataLayout();

  for (BasicBlock &BB : Fn) {
    for (Instruction &Inst : make_early_inc_range(BB)) {
      if (Inst.isTerminator() || Inst.mayHaveSideEffects())
        continue;

      Constant *Folded = ConstantFoldInstruction(&Inst, DL);

      if (!Folded)
        continue;

      Inst.replaceAllUsesWith(Folded);
      Inst.eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// 2. Instruction combining
//===----------------------------------------------------------------------===//

static Value *simplifyIntegerOperation(BinaryOperator &BO) {
  Value *LHS = BO.getOperand(0);
  Value *RHS = BO.getOperand(1);
  Type *Ty = BO.getType();

  if (!Ty->isIntegerTy())
    return nullptr;

  if (LHS == RHS) {
    switch (BO.getOpcode()) {
    case Instruction::Xor:
    case Instruction::Sub:
      return ConstantInt::get(Ty, 0);

    case Instruction::And:
    case Instruction::Or:
      return LHS;

    case Instruction::UDiv:
    case Instruction::SDiv:
      return ConstantInt::get(Ty, 1);

    default:
      break;
    }
  }

  Constant *LHSConstant = dyn_cast<Constant>(LHS);
  Constant *RHSConstant = dyn_cast<Constant>(RHS);

  if (LHSConstant && !RHSConstant && BO.isCommutative()) {
    std::swap(LHS, RHS);
    std::swap(LHSConstant, RHSConstant);
  }

  if (!RHSConstant)
    return nullptr;

  switch (BO.getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
    if (RHSConstant->isNullValue())
      return LHS;
    break;

  case Instruction::Mul:
    if (RHSConstant->isNullValue())
      return ConstantInt::get(Ty, 0);

    if (RHSConstant->isOneValue())
      return LHS;
    break;

  case Instruction::UDiv:
  case Instruction::SDiv:
    if (RHSConstant->isOneValue())
      return LHS;
    break;

  case Instruction::And:
    if (RHSConstant->isNullValue())
      return ConstantInt::get(Ty, 0);
    break;

  case Instruction::Or:
  case Instruction::Xor:
    if (RHSConstant->isNullValue())
      return LHS;
    break;

  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    if (RHSConstant->isNullValue())
      return LHS;
    break;

  default:
    break;
  }

  return nullptr;
}

static bool combineInstructions(Function &Fn) {
  bool Changed = false;
  const DataLayout &DL = Fn.getDataLayout();

  for (BasicBlock &BB : Fn) {
    for (Instruction &Inst : make_early_inc_range(BB)) {
      auto *BO = dyn_cast<BinaryOperator>(&Inst);

      if (!BO)
        continue;

      if (Value *Simplified = simplifyIntegerOperation(*BO)) {
        BO->replaceAllUsesWith(Simplified);
        BO->eraseFromParent();
        Changed = true;
        continue;
      }

      Constant *LHS = dyn_cast<Constant>(BO->getOperand(0));
      Constant *RHS = dyn_cast<Constant>(BO->getOperand(1));

      if (LHS && RHS) {
        if (Constant *Folded = ConstantFoldInstruction(BO, DL)) {
          BO->replaceAllUsesWith(Folded);
          BO->eraseFromParent();
          Changed = true;
        }
      }
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// 3. Dead code elimination
//===----------------------------------------------------------------------===//

static bool eliminateDeadInstructions(Function &Fn) {
  bool Changed = false;

  for (BasicBlock &BB : Fn) {
    SmallVector<Instruction *, 32> WorkList;

    for (Instruction &Inst : BB)
      WorkList.push_back(&Inst);

    while (!WorkList.empty()) {
      Instruction *Inst = WorkList.pop_back_val();

      if (!isInstructionTriviallyDead(Inst))
        continue;

      SmallVector<Instruction *, 8> Operands;

      for (Value *V : Inst->operands()) {
        if (auto *OperandInst = dyn_cast<Instruction>(V))
          Operands.push_back(OperandInst);
      }

      Inst->eraseFromParent();
      Changed = true;

      for (Instruction *OperandInst : Operands)
        WorkList.push_back(OperandInst);
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// 4. Strength reduction
//===----------------------------------------------------------------------===//

static Instruction *createPowerOfTwoShift(BinaryOperator &BO) {
  if (BO.getOpcode() != Instruction::Mul)
    return nullptr;

  Value *Variable = nullptr;
  ConstantInt *Multiplier = nullptr;

  if ((Multiplier = dyn_cast<ConstantInt>(BO.getOperand(0)))) {
    Variable = BO.getOperand(1);
  } else if ((Multiplier = dyn_cast<ConstantInt>(BO.getOperand(1)))) {
    Variable = BO.getOperand(0);
  } else {
    return nullptr;
  }

  const APInt &MultiplierValue = Multiplier->getValue();

  if (!MultiplierValue.isPowerOf2())
    return nullptr;

  unsigned ShiftAmount = MultiplierValue.logBase2();

  if (ShiftAmount == 0)
    return nullptr;

  Constant *ShiftConstant =
      ConstantInt::get(Variable->getType(), ShiftAmount);

  return BinaryOperator::Create(
      Instruction::Shl,
      Variable,
      ShiftConstant,
      "",
      BO.getIterator());
}

static bool reduceStrength(Function &Fn) {
  bool Changed = false;

  for (BasicBlock &BB : Fn) {
    for (Instruction &Inst : make_early_inc_range(BB)) {
      auto *BO = dyn_cast<BinaryOperator>(&Inst);

      if (!BO)
        continue;

      Instruction *Shift = createPowerOfTwoShift(*BO);

      if (!Shift)
        continue;

      BO->replaceAllUsesWith(Shift);
      BO->eraseFromParent();

      Changed = true;
    }
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
// 5. Common subexpression elimination
//===----------------------------------------------------------------------===//

static bool isCSECandidate(Instruction &Inst) {
  if (Inst.isTerminator())
    return false;

  if (Inst.mayHaveSideEffects())
    return false;

  if (Inst.mayReadFromMemory())
    return false;

  return isa<BinaryOperator>(&Inst) ||
         isa<CmpInst>(&Inst) ||
         isa<CastInst>(&Inst) ||
         isa<SelectInst>(&Inst) ||
         isa<GetElementPtrInst>(&Inst);
}

static std::string getExpressionKey(Instruction &Inst) {
  SmallVector<const Value *, 4> Operands;

  for (Value *Operand : Inst.operands())
    Operands.push_back(Operand);

  if (Inst.isCommutative() && Operands.size() == 2) {
    if (Operands[1] < Operands[0])
      std::swap(Operands[0], Operands[1]);
  }

  std::string Key;
  raw_string_ostream OS(Key);

  OS << Inst.getOpcode();
  OS << ":";
  OS << *Inst.getType();

  for (const Value *Operand : Operands) {
    OS << ":";
    OS << Operand;
  }

  if (auto *Cmp = dyn_cast<CmpInst>(&Inst))
    OS << ":predicate=" << Cmp->getPredicate();

  return OS.str();
}

static bool eliminateCommonExpressions(Function &Fn) {
  bool Changed = false;

  for (BasicBlock &BB : Fn) {
    std::map<std::string, Instruction *> Expressions;

    for (Instruction &Inst : make_early_inc_range(BB)) {
      if (!isCSECandidate(Inst))
        continue;

      std::string Key = getExpressionKey(Inst);

      auto It = Expressions.find(Key);

      if (It == Expressions.end()) {
        Expressions.emplace(Key, &Inst);
        continue;
      }

      Instruction *Existing = It->second;

      Inst.replaceAllUsesWith(Existing);
      Inst.eraseFromParent();

      Changed = true;
    }
  }

  return Changed;
}

} // namespace

//===----------------------------------------------------------------------===//
// Pass driver
//===----------------------------------------------------------------------===//

PreservedAnalyses
HelloWorldPass::run(Function &F, FunctionAnalysisManager &AM) {
  (void)AM;

  bool Changed = false;

  for (unsigned Iteration = 0; Iteration < 6; ++Iteration) {
    bool RoundChanged = false;

    RoundChanged |= propagateConstants(F);
    RoundChanged |= combineInstructions(F);
    RoundChanged |= reduceStrength(F);
    RoundChanged |= eliminateDeadInstructions(F);
    RoundChanged |= eliminateCommonExpressions(F);

    if (!RoundChanged)
      break;

    Changed = true;
  }

  if (Changed)
    return PreservedAnalyses::none();

  return PreservedAnalyses::all();
}

//===----------------------------------------------------------------------===//
// Pass plugin registration
//===----------------------------------------------------------------------===//

extern "C" LLVM_ATTRIBUTE_WEAK
llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION,
      "HelloWorldPass",
      LLVM_VERSION_STRING,
      [](llvm::PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](llvm::StringRef Name,
               llvm::FunctionPassManager &FPM,
               llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
              if (Name == "hello-world") {
                FPM.addPass(llvm::HelloWorldPass());
                return true;
              }

              return false;
            });
      }};
}
