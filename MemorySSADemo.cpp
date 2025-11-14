#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"

#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/PostDominators.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

using namespace llvm;

// ================== MemorySSA DOT writer ==================

static void writeMemorySSADot(Function &F, MemorySSA &MSSA) {
  std::string Filename = (F.getName() + ".memssa.dot").str();
  std::error_code EC;
  raw_fd_ostream Out(Filename, EC, sys::fs::OF_Text);
  if (EC) {
    errs() << "Error opening " << Filename << ": " << EC.message() << "\n";
    return;
  }

  Out << "digraph \"MemorySSA_" << F.getName() << "\" {\n";
  Out << "  node [shape=box,fontname=\"Courier\"];\n";

  // Nodes and edges for MemoryAccesses.
  for (auto &BB : F) {
    // Block entry MemoryPhi, if any.
    if (auto *MA = MSSA.getMemoryAccess(&BB)) {
      if (auto *MPhi = dyn_cast<MemoryPhi>(MA)) {
        Out << "  \"" << MPhi << "\" [label=\"";
        MPhi->print(Out);
        Out << "\"];\n";
        for (unsigned i = 0; i < MPhi->getNumIncomingValues(); ++i) {
          auto *Incoming = MPhi->getIncomingValue(i);
          Out << "  \"" << Incoming << "\" -> \"" << MPhi << "\";\n";
        }
      }
    }

    for (auto &I : BB) {
      if (auto *MA = MSSA.getMemoryAccess(&I)) {
        Out << "  \"" << MA << "\" [label=\"";
        MA->print(Out);
        Out << "\"];\n";

        if (auto *MD = dyn_cast<MemoryDef>(MA)) {
          if (auto *Def = MD->getDefiningAccess())
            Out << "  \"" << Def << "\" -> \"" << MA << "\";\n";
        } else if (auto *MU = dyn_cast<MemoryUse>(MA)) {
          if (auto *Def = MU->getDefiningAccess())
            Out << "  \"" << Def << "\" -> \"" << MA << "\";\n";
        }
      }
    }
  }

  Out << "}\n";
  errs() << "Wrote DOT file: " << Filename << "\n";
}

// ================== MemorySSADemoPass  ==================

struct MemorySSADemoPass : PassInfoMixin<MemorySSADemoPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    if (F.isDeclaration())
      return PreservedAnalyses::all();

    auto &MSSAResult = AM.getResult<MemorySSAAnalysis>(F);
    MemorySSA &MSSA = MSSAResult.getMSSA();

    errs() << "Analyzing function: " << F.getName() << "\n";

    for (auto &BB : F) {
      errs() << "BasicBlock: " << BB.getName() << "\n";

      if (auto *Phi = MSSA.getMemoryAccess(&BB)) {
        if (auto *MPhi = dyn_cast<MemoryPhi>(Phi)) {
          errs() << "  MemoryPhi for block " << BB.getName() << ":\n";
          for (unsigned i = 0; i < MPhi->getNumIncomingValues(); ++i) {
            auto *IncomingAcc = MPhi->getIncomingValue(i);
            auto *Pred = MPhi->getIncomingBlock(i);
            errs() << "    from " << Pred->getName() << ": ";
            IncomingAcc->print(errs());
            errs() << "\n";
          }
        }
      }

      for (auto &I : BB) {
        if (auto *MA = MSSA.getMemoryAccess(&I)) {
          errs() << "  ";
          MA->print(errs());
          errs() << "\n";
        }
      }
    }

    writeMemorySSADot(F, MSSA);
    return PreservedAnalyses::all();
  }
};

// ================== DSE helpers ==================

// check if both stores write to same ptr
static bool mustAliasStores(StoreInst *a, StoreInst *b) {
  return a->getPointerOperand() == b->getPointerOperand();
}

// look for anything between prev and curr that touches memory
static bool hasInterveningUse(StoreInst *prev, StoreInst *curr) {
  BasicBlock *bb = prev->getParent();
  if (bb != curr->getParent())
    return true; // diff blocks -> too risky

  for (Instruction *i = prev->getNextNode(); i && i != curr; i = i->getNextNode()) {
    if (!i->mayReadFromMemory() && !i->mayWriteToMemory())
      continue;
    return true; // found something that may touch mem
  }
  return false; // clean in between
}

// ================== DSE  ==================

struct DeadStoreElimPass : PassInfoMixin<DeadStoreElimPass> {
  PreservedAnalyses run(Function &f, FunctionAnalysisManager &am) {
    if (f.isDeclaration())
      return PreservedAnalyses::all();

    auto &mssaRes = am.getResult<MemorySSAAnalysis>(f);
    MemorySSA &mssa = mssaRes.getMSSA();
    MemorySSAUpdater updater(&mssa);

    PostDominatorTree &pdt = am.getResult<PostDominatorTreeAnalysis>(f);
    MemorySSAWalker *walker = mssa.getWalker();

    SmallVector<MemoryDef *, 64> defs;

    // collect defs
    for (Instruction &i : instructions(f)) {
      if (auto *md = dyn_cast_or_null<MemoryDef>(mssa.getMemoryAccess(&i)))
        defs.push_back(md);
    }

    DenseSet<StoreInst *> dead;
    SmallVector<StoreInst *, 32> removeList;

    // walk backwards through defs
    for (auto it = defs.rbegin(); it != defs.rend(); ++it) {
      MemoryDef *d = *it;
      Instruction *inst = d->getMemoryInst();
      if (!inst)
        continue;

      auto *currStore = dyn_cast<StoreInst>(inst);
      if (!currStore)
        continue;

      // step 1. find clobber this store overwrote
      MemoryAccess *prevAcc = walker->getClobberingMemoryAccess(d);
      if (!prevAcc)
        continue;

      auto *prevDef = dyn_cast<MemoryDef>(prevAcc);
      if (!prevDef)
        continue;

      Instruction *prevInst = prevDef->getMemoryInst();
      if (!prevInst)
        continue;

      auto *prevStore = dyn_cast<StoreInst>(prevInst);
      if (!prevStore)
        continue;

      if (dead.count(prevStore))
        continue;

      // step 2. check conditions
      if (!mustAliasStores(currStore, prevStore))
        continue;
      if (hasInterveningUse(prevStore, currStore))
        continue;

      BasicBlock *prevBB = prevStore->getParent();
      BasicBlock *currBB = currStore->getParent();
      if (!pdt.dominates(currBB, prevBB))
        continue;

      // mark previous store dead
      dead.insert(prevStore);
      removeList.push_back(prevStore);
    }

    // erase dead ones
    for (StoreInst *s : removeList) {
      if (auto *md = dyn_cast_or_null<MemoryDef>(mssa.getMemoryAccess(s)))
        updater.removeMemoryAccess(md);
      errs() << "removing dead store: " << *s << "\n";
      s->eraseFromParent();
    }

    return removeList.empty() ? PreservedAnalyses::all()
                              : PreservedAnalyses::none();
  }
};


extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "MemorySSADemoPass", "v1.0",
          [](PassBuilder &PB) {
            PB.registerAnalysisRegistrationCallback(
                [](FunctionAnalysisManager &FAM) {
                  FAM.registerPass([] { return MemorySSAAnalysis(); });
                  FAM.registerPass([] { return AAManager(); });
                  FAM.registerPass([] { return PostDominatorTreeAnalysis(); });
                });

            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "memssa-demo") {
                    FPM.addPass(MemorySSADemoPass());
                    return true;
                  }
                  if (Name == "dead-store-elim") {
                    FPM.addPass(DeadStoreElimPass());
                    return true;
                  }
                  return false;
                });
          }};
}
