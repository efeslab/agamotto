//===-- RootCause.cpp -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <functional>

#include "RootCause.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KInstruction.h"

using namespace klee;
using namespace llvm;

uint64_t RootCauseLocation::Hash::operator()(const RootCauseLocation &r) const {
  return std::hash<const void*>{}(r.allocSite) ^ 
         std::hash<const void*>{}(r.inst);
}

bool 
RootCauseLocation::LocationEq::operator()(const RootCauseLocation *lhs,
                                          const RootCauseLocation *rhs) const {
  return lhs->allocSite == rhs->allocSite &&
         lhs->inst == rhs->inst &&
         lhs->stack == rhs->stack &&
         lhs->reason == rhs->reason;
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

std::string RootCauseLocation::str() const {
  std::string infoStr;
  llvm::raw_string_ostream info(infoStr);

  info << "Type of modification: ";
  switch(reason) {
    case PM_Unpersisted:
      info << "write (unpersisted)";
      break;
    case PM_UnnecessaryFlush:
      info << "flush (unnecessary)";
      break;
    case PM_FlushOnUnmodified:
      info << "flush (never modified)";
      break;
    default:
      klee_error("unsupported!");
      break;
  }
  info << "\n";

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

bool klee::operator==(const RootCauseLocation &lhs, 
                      const RootCauseLocation &rhs) {
  return lhs.allocSite == rhs.allocSite &&
         lhs.inst == rhs.inst &&
         lhs.stackStr == rhs.stackStr;
}

/***/

uint64_t RootCauseManager::getRootCauseLocationID(const ExecutionState &state, 
                                                  const llvm::Value *allocationSite, 
                                                  const KInstruction *pc,
                                                  RootCauseReason reason) {
  RootCauseLocation rcl(state, allocationSite, pc, reason);
  if (rootToId.count(rcl)) {
    return rootToId.at(rcl);
  }

  uint64_t id = nextId;
  nextId++;

  rootToId[rcl] = id;
  idToRoot[id] = &rootToId.find(rcl)->first;

  return id;
}

std::string RootCauseManager::getRootCauseString(uint64_t id) const {
  assert(idToRoot.count(id) && "unknown ID!!!");
  return idToRoot.at(id)->str();
}

std::unordered_set<std::string> 
RootCauseManager::getUniqueRootCauseStrings(
  const std::unordered_set<uint64_t> &ids) const 
{
  std::unordered_set<const RootCauseLocation*, 
                     std::hash<const RootCauseLocation*>,
                     RootCauseLocation::LocationEq> rootCauses;

  for (auto id : ids) {
    assert(idToRoot.count(id) && "unknown ID!!!");
    rootCauses.insert(idToRoot.at(id));
  }

  std::unordered_set<std::string> exampleStrings;
  for (const RootCauseLocation *r : rootCauses) {
    exampleStrings.insert(r->str());
  }

  return exampleStrings;
}

void RootCauseManager::clear() {
  nextId = 1lu;
  rootToId.clear();
  idToRoot.clear();
}