//===-- RootCause.cpp -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <functional>
#include <sstream>
#include <iostream>
#include <cmath>

#include "RootCause.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"
#include "CoreStats.h"

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

void RootCauseLocation::addMaskingError(uint64_t id) {
  maskingRoots.insert(id);
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
  info << str() << "\n";

  if (maskedRoots.size()) {
    info << "May be masking:\n";

    for (auto id : maskedRoots) {
      info << "\tID #" << id << "\n";
      std::istringstream f(mgr.get(id).stackStr);
      std::string line;    
      while (std::getline(f, line)) {
        info << "\t\t" << line << "\n";
      }
    }
  } else {
    info << "<not masking anything>\n";
  }
  
  if (maskingRoots.size()) {
    info << "May be masked by:\n";
    for (auto id: maskingRoots) {
      info << "\tID #" << id << "\n";
    }
  } else {
    info << "<not masked by anything>\n";
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

  uint64_t newId = nextId++;
  assert(nextId > 0 && "ID overflow!");

  for (auto id : ids) {
    if (!idToRoot.count(id)) {
      llvm::errs() << id << "\n" << getSummary();
    }
    assert(idToRoot.count(id) && "we messed something up with our id tracking");
    rcl.addMaskedError(id);
    idToRoot.at(id)->rootCause.addMaskingError(newId);

    for (auto subId : idToRoot.at(id)->rootCause.getMaskedSet()) {
      assert(idToRoot.count(subId) && "we messed something up with our id tracking");
      rcl.addMaskedError(subId);
      idToRoot.at(subId)->rootCause.addMaskingError(newId);
    }
  }

  if (rootToId.count(rcl)) {
    return rootToId.at(rcl);
  }

  

  rootToId[rcl] = newId;
  idToRoot[newId] = std::unique_ptr<RootCauseInfo>(new RootCauseInfo(rcl));

  return newId;
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
    stats::nvmBugsTotalOccurences++;
    if (idToRoot[i]->rootCause.getReason() == PM_Unpersisted) {
      stats::nvmBugsCrtOccurences++;
      if (!idToRoot[i]->occurences) {
        stats::nvmBugsTotalUniq++;
        stats::nvmBugsCrtUniq++;
      }
    } else {
      stats::nvmBugsPerfOccurences++;
      if (!idToRoot[i]->occurences) {
        stats::nvmBugsTotalUniq++;
        stats::nvmBugsPerfUniq++;
      }
    }

    ++idToRoot[i]->occurences;
    ++totalOccurences;
    buggyIds.insert(i);
    assert(totalOccurences > 0 && "overflow!");
    // We use this for the number of rows later
    largestStack = std::max(largestStack, idToRoot[i]->rootCause.stack.size());
  }
}

std::string RootCauseManager::getRootCauseString(uint64_t id) const {
  assert(id > 0 && "we don't do <= 0");
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

void RootCauseManager::dumpText(llvm::raw_ostream &out) const {
  uint64_t bugNo = 1;

  out << getSummary();

  for (const auto &id : buggyIds) {
    out << "\n(" << bugNo << ") ID #" << id; 
    out << " with " << idToRoot.at(id)->occurences << " occurences:\n";
    out << idToRoot.at(id)->rootCause.fullString(*this);
    bugNo++;
  }

  out.flush();
}

void RootCauseManager::dumpCSV(llvm::raw_ostream &out) const {
  // Create header
  out << "ID,Type,Occurences";
  for (size_t stackframeNum = 0; stackframeNum < largestStack; ++stackframeNum) {
    // This is the full description as a convenience
    out << ",StackFrame" << stackframeNum << ",";
    // This gives us the individual parts.
    out << "StackFrame" << stackframeNum << "_Function,";
    // -- These will be NULL for main
    out << "StackFrame" << stackframeNum << "_File,";
    out << "StackFrame" << stackframeNum << "_Line";
  }
  out << "\n";

  // Now, the data entries
  for (const auto &id : buggyIds) {
    RootCauseLocation &rcl = idToRoot.at(id)->rootCause;
    out << id << ","; // ID
    out << rcl.reasonString() << ","; // Type
    out << idToRoot.at(id)->occurences; // Occurences

    const KInstruction *target = rcl.inst;
    assert(largestStack >= rcl.stack.size() && "bad tracking!");
    for (auto it = rcl.stack.rbegin(); it != rcl.stack.rend(); ++it) {
      RootCauseLocation::RootCauseStackFrame &sf = *it;
      Function *f = sf.kf->function;
      const InstructionInfo &ii = *target->info;

      out << ",";

      // The "full" description
      out << f->getName().str();
      if (ii.file != "") {
        out << " at " << ii.file << ":" << ii.line;
      }
      out << ",";

      out << f->getName().str() << ","; // Function
      out << ii.file << ",";
      out << ii.line; // Next iteration writes the comma

      target = sf.caller;
    }    
    
    out << "\n"; // Entry complete
  }

  out.flush();
}

std::string RootCauseManager::getSummary(void) const {
  std::string infoStr;
  llvm::raw_string_ostream info(infoStr);

  size_t nUnpersisted = 0, nExtra = 0, nClean = 0;
  size_t nUnpersistedOc = 0, nExtraOc = 0, nCleanOc = 0;
  for (const auto &id : buggyIds) {
    switch(idToRoot.at(id)->rootCause.getReason()) {
      case PM_Unpersisted:
        nUnpersisted++;
        nUnpersistedOc += idToRoot.at(id)->occurences;
        break;
      case PM_UnnecessaryFlush:
        nExtra++;
        nExtraOc += idToRoot.at(id)->occurences;
        break;
      case PM_FlushOnUnmodified:
        nClean++;
        nCleanOc += idToRoot.at(id)->occurences;
        break;
      default:
        klee_error("unsupported!");
        break;
    }
  }

  info << "Persistent Memory Bugs:\n";
  info << "\tNumber of bugs: " << buggyIds.size() << "\n";
  info << "\t\tNumber of unpersisted write bugs (correctness): " << nUnpersisted << "\n";
  info << "\t\tNumber of extra flush bugs (performance): " << nExtra << "\n";
  info << "\t\tNumber of flushes to untouched memory (performance): " << nClean << "\n";
  info << "\tOverall bug occurences: " << totalOccurences << "\n";
  info << "\t\tNumber of unpersisted write occurences (correctness): " << nUnpersistedOc << "\n";
  info << "\t\tNumber of extra flush occurences (performance): " << nExtraOc << "\n";
  info << "\t\tNumber of untouched memory flush occurences (performance): " << nCleanOc << "\n";

  return info.str();
}
