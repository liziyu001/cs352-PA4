/**
 * USCC Compiler
 * Jianping Zeng (zeng207@purdue.edu)
 * An iterative backward liveness analysis.
 * This pass intends to compute a set of live-out/in variables for each LLVM basic block
 * and maintain a set of LLVM instructions that are dead---not used by any following others.
*/

#include "Liveness.h"

using namespace std;
using namespace llvm;

bool enableLiveness;

char Liveness::ID = 0;
INITIALIZE_PASS(Liveness, "liveness", "Liveness Analysis", true, true)

FunctionPass *llvm::createLivenessPass() 
{
    return new Liveness();
}

void Liveness::postOrderDFS(llvm::BasicBlock *BB, std::set<llvm::BasicBlock *> &visited, std::vector<llvm::BasicBlock *> &postOrder) {
    if (visited.count(BB)) return;
    visited.insert(BB);

    for (auto succ = llvm::succ_begin(BB); succ != llvm::succ_end(BB); ++succ) {
        postOrderDFS(*succ, visited, postOrder);
    }
    postOrder.push_back(BB);
}

bool Liveness::runOnFunction(Function &F) 
{
    if (F.empty())
        return false;
    BasicBlock &frontBB = F.front();
    BasicBlock &endBB = F.back();
    assert(!frontBB.empty() && !endBB.empty() && "the front/end basic block must not be empty!");
    // The OUT set of the last block is empty.
    bb2Out[&endBB] = std::set<StringRef>();

    // PA4
    // Step #1: identify program variables.
    for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
            if (I.hasName() && I.getOpcode() == Instruction::Alloca) {
                named.insert(I.getName());
            }
        }
    }

    // Step #2: calculate DEF/USE set for each basic block
    for (BasicBlock &BB : F) {
        // std::set<StringRef> def_set, use_set;
        for (auto instrIt = BB.rbegin(); instrIt != BB.rend(); ++instrIt) {
            Instruction &I = *instrIt;
            if (auto *loadInst = dyn_cast_or_null<LoadInst>(&I)) {
                if (loadInst->getPointerOperand()->hasName() && named.count(loadInst->getPointerOperand()->getName())) {
                    use[&BB].insert(loadInst->getPointerOperand()->getName());
                    def[&BB].erase(loadInst->getPointerOperand()->getName());
                }
            } else if (auto *storeInst = dyn_cast_or_null<StoreInst>(&I)) {
                if (storeInst->getPointerOperand()->hasName() && named.count(storeInst->getPointerOperand()->getName())) {
                    def[&BB].insert(storeInst->getPointerOperand()->getName());
                    use[&BB].erase(storeInst->getPointerOperand()->getName());
                }
            }
        }
        // def[&BB] = def_set;
        // use[&BB] = use_set;
    }

    // Step #3: compute post order traversal.
    std::set<llvm::BasicBlock *> visited;
    std::vector<llvm::BasicBlock *> postOrder;

    postOrderDFS(&frontBB, visited, postOrder);
    

    // Step #4: iterate over control flow graph of the input function until the fixed point.
    unsigned cnt = 0;
    bool changed = true;
    // for (auto &BB : F) {
    //     std::set<StringRef> in_set, out_set;
    //     bb2In[&BB] = in_set;
    //     bb2Out[&BB] = out_set;
    // }
    while (changed) {
        changed = false;
        for (auto bbIt = postOrder.begin(); bbIt != postOrder.end(); ++bbIt) {
            BasicBlock *BB = *bbIt;
            std::set<StringRef> new_in, new_out;
            for (auto succ = llvm::succ_begin(BB); succ != llvm::succ_end(BB); ++succ) {
                new_out.insert(bb2In[*succ].begin(), bb2In[*succ].end());
            }
            new_in.insert(new_out.begin(), new_out.end());
            new_in.insert(use[BB].begin(), use[BB].end());
            // new_in.erase(def[BB].begin(), def[BB].end());
            for (auto var : def[BB]) {
                new_in.erase(var);
            }
            
            bb2Out[BB] = new_out;
            if (new_in != bb2In[BB]) {
                changed = true;
                bb2In[BB] = new_in;
            }
        }
        cnt++;
    }

    // Step #5: output IN/OUT set for each basic block.
    if (enableLiveness) 
    {
        llvm::outs() << "********** Live-in/Live-out information **********\n";
        llvm::outs() << "********** Function: " << F.getName().str() << ", analysis iterates " << cnt << " times\n";
        for (auto &bb : F) 
        {
            llvm::outs() << bb.getName() << ":\n";
            llvm::outs() << "  IN:";
            for (auto &var : bb2In[&bb])
                llvm::outs() << " " << var.substr(0, var.size() - 5);
            llvm::outs() << "\n";
            llvm::outs() << "  OUT:";
            for (auto &var : bb2Out[&bb])
                llvm::outs() << " " << var.substr(0, var.size() - 5);
            llvm::outs() << "\n";
        }
    }
    // Liveness does not change the input function at all.
    return false;
}

bool Liveness::isDead(llvm::Instruction &inst) 
{
    BasicBlock *bb = inst.getParent();
    if (!bb) {
        return true;
    }
        
    // if (!bb2Out.count(bb)) {
    //     return true;
    // }

    if (inst.getOpcode() != Instruction::Store) {
        return false;
    }

    auto *storeInst = dyn_cast_or_null<LoadInst>(&inst);
    if (!named.count(storeInst->getPointerOperand()->getName())) {
        return false;
    }
        

    // PA4
    // auto out = bb2Out[bb];
    auto itr = bb->rbegin();
    auto end = std::prev(BasicBlock::reverse_iterator(inst));
    std::set<StringRef> liveWithinBB = bb2Out[bb];
    for (; itr != end; ++itr) {
        // You code over here to update the out set according to what instruction, e.g., store or load, is encountered.
        Instruction &I = *itr;
        if (auto *loadInst = dyn_cast_or_null<LoadInst>(&I)) {
            if (loadInst->getPointerOperand()->hasName() && named.count(loadInst->getPointerOperand()->getName())) {
                liveWithinBB.insert(loadInst->getPointerOperand()->getName());
            }
        } else if (auto *storeInst = dyn_cast_or_null<StoreInst>(&I)) {
            if (storeInst->getPointerOperand()->hasName() && named.count(storeInst->getPointerOperand()->getName())) {
                liveWithinBB.erase(storeInst->getPointerOperand()->getName());
            }
        }
    }
    if (liveWithinBB.count(storeInst->getPointerOperand()->getName())) {
        return true;
    }
    return false;
}
