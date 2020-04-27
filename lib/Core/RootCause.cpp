//===-- RootCause.cpp -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <functional>
#include <cmath>

#include "RootCause.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"

using namespace klee;
using namespace llvm;

uint64_t RootCauseLocation::Hash::operator()(const RootCauseLocation &r) const {
  return std::hash<const void*>{}(r.allocSite) ^ 
         std::hash<const void*>{}(r.inst);
}

RootCauseLocation::RootCauseLocation(const ExecutionState &state, 
                                     const llvm::Value *allocationSite, 
                                     const KInstruction *pc,
                                     RootCauseReason r) 
  : allocSite(allocationSite), 
    inst(pc),
    reason(r) {
  
  for (const klee::StackFrame &sf : state.stack()) {
    stack.emplace_back(sf.caller, sf.kf);
  }

  std::string tmp;
  llvm::raw_string_ostream ss(tmp);
  state.dumpStack(ss);
  stackStr = ss.str();
}

void RootCauseLocation::addMaskedError(uint64_t id) {
  maskedRoots.insert(id);
}

std::string RootCauseLocation::str(void) const {
  std::string infoStr;
  llvm::raw_string_ostream info(infoStr);

  if (inst) {
    const InstructionInfo *iip = inst->info;

    if (iip->file != "") {
      info << "File: " << iip->file << "\n";
      info << "Line: " << iip->line << "\n";
      info << "assembly.ll line: " << iip->assemblyLine << "\n";
    }
  } 
  
  if (allocSite) {
    info << " allocated at ";
    if (const Instruction *i = dyn_cast<Instruction>(allocSite)) {
      info << i->getParent()->getParent()->getName() << "():";
      info << *i;
    } else if (const GlobalValue *gv = dyn_cast<GlobalValue>(allocSite)) {
      info << "global:" << gv->getName();
    } else {
      info << "value:" << *allocSite;
    }
  } else {
    info << " (no allocation info)";
  }
  
  info << "\nStack: \n" << stackStr;

  return info.str();
}

std::string 
RootCauseLocation::fullString(const RootCauseManager &mgr) const {
  std::string infoStr;
  llvm::raw_string_ostream info(infoStr);

  info << "Type of modification: " << reasonString() << "\n";

  if (maskedRoots.size()) {
    info << str() << "\nPossible Masked:\n";

    for (auto id : maskedRoots) {
      info << "\tID #" << id << "\n";
    }
  } else {
    info << "<no masked bugs>\n";
  }
  

  return info.str();
}

const char *RootCauseLocation::reasonString(void) const {
  switch(reason) {
    case PM_Unpersisted:
      return "write (unpersisted)";
    case PM_UnnecessaryFlush:
      return "flush (unnecessary)";
    case PM_FlushOnUnmodified:
      return "flush (never modified)";
    default:
      klee_error("unsupported!");
      break;
  }

  return "";
}

bool klee::operator==(const RootCauseLocation &lhs, 
                      const RootCauseLocation &rhs) {
  return lhs.allocSite == rhs.allocSite &&
         lhs.inst == rhs.inst &&
         lhs.stack == rhs.stack &&
         lhs.reason == rhs.reason &&
         lhs.maskedRoots == rhs.maskedRoots;
}

/***/

uint64_t 
RootCauseManager::getRootCauseLocationID(const ExecutionState &state, 
                                         const llvm::Value *allocationSite, 
                                         const KInstruction *pc,
                                         RootCauseReason reason) {
  RootCauseLocation rcl(state, allocationSite, pc, reason);
  if (rootToId.count(rcl)) {
    return rootToId.at(rcl);
  }

  uint64_t id = nextId++;
  assert(nextId > 0 && "ID overflow!");

  rootToId[rcl] = id;
  idToRoot[id] = std::unique_ptr<RootCauseInfo>(new RootCauseInfo(rcl));

  return id;
}

uint64_t 
RootCauseManager::getRootCauseLocationID(const ExecutionState &state, 
                                         const llvm::Value *allocationSite, 
                                         const KInstruction *pc,
                                         RootCauseReason reason,
                                         const std::unordered_set<uint64_t> &ids) {
  RootCauseLocation rcl(state, allocationSite, pc, reason);

  /**
   * We want all the ids so we can flatten the masking set (i.e., the masking 
   * set) tells you all writes this write masks, even if they may mask each 
   * other.
   * 
   * Since we do this each time, each id in ids should be flattened already, so
   * we shouldn't have to do anything recursively here.
   */

  for (auto id : ids) {
    if (!idToRoot.count(id)) {
      llvm::errs() << id << "\n" << getSummary();
    }
    assert(idToRoot.count(id) && "we messed something up with our id tracking");
    rcl.addMaskedError(id);

    for (auto subId : idToRoot.at(id)->rootCause.getMaskedSet()) {
      assert(idToRoot.count(subId) && "we messed something up with our id tracking");
      rcl.addMaskedError(subId);
    }
  }

  if (rootToId.count(rcl)) {
    return rootToId.at(rcl);
  }

  uint64_t id = nextId++;
  assert(nextId > 0 && "ID overflow!");

  rootToId[rcl] = id;
  idToRoot[id] = std::unique_ptr<RootCauseInfo>(new RootCauseInfo(rcl));

  return id;
}

void RootCauseManager::markAsBug(uint64_t id) {
  if (!idToRoot.count(id)) {
    klee_error("ID %lu is not in our mappings!", id);
  }

  std::unordered_set<uint64_t> allIds(idToRoot.at(id)->rootCause.getMaskedSet());
  allIds.insert(id);

  /**
   * We want to mark all of the masked root cause locations as occurences as 
   * well. This may result in some overcounting (i.e., some of the unpersisted
   * writes may be temporary work), but without full semantic information this
   * is impossible to get 100% accurate.
   */

  for (auto i : allIds) {
    ++idToRoot[i]->occurences;
    ++totalOccurences;
    buggyIds.insert(i);
    assert(totalOccurences > 0 && "overflow!");
  }
}

std::string RootCauseManager::getRootCauseString(uint64_t id) const {
  assert(idToRoot.count(id) && "unknown ID!!!");
  return idToRoot.at(id)->rootCause.str();
}

void RootCauseManager::clear() {
  return;

  std::unordered_set<uint64_t> cleanIds;
  for (const auto &p : idToRoot) {
    if (p.second->occurences) continue;

    assert(rootToId.count(p.second->rootCause) && "our two maps got out of sync!");
    rootToId.erase(p.second->rootCause);
    cleanIds.insert(p.first);
  }

  for (auto id : cleanIds) {
    idToRoot.erase(id);
  }
}

std::string RootCauseManager::str(void) const {
  uint64_t bugNo = 1;

  std::string infoStr;
  llvm::raw_string_ostream info(infoStr);

  info << getSummary();

  for (const auto &id : buggyIds) {
    info << "\n(" << bugNo << ") ID #" << id; 
    info << " with " << idToRoot.at(id)->occurences << " occurences:\n";
    info << idToRoot.at(id)->rootCause.fullString(*this);
    bugNo++;
  }

  return info.str();
}

std::string RootCauseManager::getSummary(void) const {
  std::string infoStr;
  llvm::raw_string_ostream info(infoStr);

  info << "Persistent Memory Bugs:\n";
  info << "\tNumber of bugs: " << buggyIds.size() << "\n";
  info << "\tOverall bug occurences: " << totalOccurences << "\n";

  return info.str();
}