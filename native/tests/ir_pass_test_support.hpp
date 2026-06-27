#pragma once

#include "mkpro/core/ir.hpp"
#include "mkpro/core/passes/helpers.hpp"
#include "mkpro/core/result.hpp"

#include "test_support.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// Shared IR builders mirroring the helper functions in
// tests/compiler/passes.test.ts so that pass-level parity tests can be ported
// assertion-for-assertion. Deep `toEqual` comparisons are reproduced by
// serialising both operand vectors with ir_ops_to_json.
namespace mkpro::tests::irbuild {

inline int register_index(const std::string& register_name) {
  if (register_name.size() != 1U)
    return 0;
  const char ch = register_name.at(0);
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'e')
    return 10 + (ch - 'a');
  return 0;
}

inline IrOp store(const std::string& register_name) {
  IrOp op;
  op.kind = IrKind::Store;
  op.register_name = register_name;
  op.opcode = 0x40 + register_index(register_name);
  op.meta.mnemonic = "X->\u041f " + register_name;
  return op;
}

inline IrOp recall(const std::string& register_name,
                   std::optional<std::string> comment = std::nullopt) {
  IrOp op;
  op.kind = IrKind::Recall;
  op.register_name = register_name;
  op.opcode = 0x60 + register_index(register_name);
  op.meta.mnemonic = "\u041f->X " + register_name;
  op.meta.comment = std::move(comment);
  return op;
}

inline IrOp plain(int opcode, std::string mnemonic) {
  IrOp op;
  op.kind = IrKind::Plain;
  op.opcode = opcode;
  op.meta.mnemonic = std::move(mnemonic);
  return op;
}

inline IrOp plain_with_comment(int opcode, std::string mnemonic, std::string comment) {
  IrOp op = plain(opcode, std::move(mnemonic));
  op.meta.comment = std::move(comment);
  return op;
}

inline IrOp jump(std::string target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.target = std::move(target);
  op.opcode = 0x51;
  op.meta.mnemonic = "\u0411\u041f";
  return op;
}

inline IrOp numeric_jump(int target) {
  IrOp op;
  op.kind = IrKind::Jump;
  op.target = target;
  op.opcode = 0x51;
  op.meta.mnemonic = "\u0411\u041f";
  return op;
}

inline IrOp cjump(std::string target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.condition = "==0";
  op.target = std::move(target);
  op.opcode = 0x5e;
  op.meta.mnemonic = "F x=0";
  return op;
}

inline IrOp numeric_cjump(int target) {
  IrOp op;
  op.kind = IrKind::CondJump;
  op.condition = "==0";
  op.target = target;
  op.opcode = 0x5e;
  op.meta.mnemonic = "F x=0";
  return op;
}

inline IrOp loop(std::string target) {
  IrOp op;
  op.kind = IrKind::Loop;
  op.counter = "L0";
  op.target = std::move(target);
  op.opcode = 0x5d;
  op.meta.mnemonic = "F L0";
  return op;
}

inline IrOp call(std::string target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.meta.comment = "proc call " + target;
  op.target = std::move(target);
  op.opcode = 0x53;
  op.meta.mnemonic = "\u041f\u041f";
  return op;
}

inline IrOp numeric_call(int target) {
  IrOp op;
  op.kind = IrKind::Call;
  op.target = target;
  op.opcode = 0x53;
  op.meta.mnemonic = "\u041f\u041f";
  return op;
}

inline IrOp indirect_recall(const std::string& register_name) {
  IrOp op;
  op.kind = IrKind::IndirectRecall;
  op.register_name = register_name;
  op.opcode = 0xd0 + register_index(register_name);
  op.meta.mnemonic = "\u041a \u041f->X " + register_name;
  return op;
}

inline IrOp indirect_store(const std::string& register_name) {
  IrOp op;
  op.kind = IrKind::IndirectStore;
  op.register_name = register_name;
  op.opcode = 0xb0 + register_index(register_name);
  op.meta.mnemonic = "\u041a X->\u041f " + register_name;
  return op;
}

inline IrOp indirect_jump(const std::string& register_name) {
  IrOp op;
  op.kind = IrKind::IndirectJump;
  op.register_name = register_name;
  op.opcode = 0x80 + register_index(register_name);
  op.meta.mnemonic = "\u041a \u0411\u041f " + register_name;
  return op;
}

inline IrOp indirect_call(const std::string& register_name) {
  IrOp op;
  op.kind = IrKind::IndirectCall;
  op.register_name = register_name;
  op.opcode = 0xa0 + register_index(register_name);
  op.meta.mnemonic = "\u041a \u041f\u041f " + register_name;
  return op;
}

inline IrOp indirect_cjump(const std::string& register_name) {
  IrOp op;
  op.kind = IrKind::IndirectCondJump;
  op.condition = "==0";
  op.register_name = register_name;
  op.opcode = 0xe0 + register_index(register_name);
  op.meta.mnemonic = "\u041a x=0 " + register_name;
  return op;
}

inline IrOp known_target_indirect_recall(const std::string& register_name,
                                         const std::string& target) {
  IrOp op = indirect_recall(register_name);
  op.meta.comment = "indirect-memory-target=" + target;
  return op;
}

inline IrOp known_target_indirect_store(const std::string& register_name,
                                        const std::string& target) {
  IrOp op = indirect_store(register_name);
  op.meta.comment = "indirect-memory-target=" + target;
  return op;
}

inline IrOp known_target_indirect_jump(const std::string& register_name, int target) {
  IrOp op = indirect_jump(register_name);
  op.meta.comment = "indirect-target=" + std::to_string(target);
  return op;
}

inline IrOp known_target_indirect_call(const std::string& register_name, int target) {
  IrOp op = indirect_call(register_name);
  op.meta.comment = "indirect-target=" + std::to_string(target);
  return op;
}

inline IrOp known_target_indirect_cjump(const std::string& register_name, int target) {
  IrOp op = indirect_cjump(register_name);
  op.meta.comment = "indirect-target=" + std::to_string(target);
  return op;
}

inline IrOp orphan_address(int target = 0) {
  IrOp op;
  op.kind = IrKind::OrphanAddress;
  op.target = target;
  op.target_meta.comment = "test address gap";
  return op;
}

inline IrOp label(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::move(name);
  return op;
}

inline IrOp proc_start(std::string name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.procedure_boundary = "start";
  op.procedure_name = name;
  op.name = std::move(name);
  return op;
}

inline IrOp proc_end(const std::string& name) {
  IrOp op;
  op.kind = IrKind::Label;
  op.name = std::string("\0proc_end_", 10) + name;
  op.procedure_boundary = "end";
  op.procedure_name = name;
  op.hidden = true;
  return op;
}

inline IrOp halt() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "halt";
  op.meta.mnemonic = "\u0421/\u041f";
  return op;
}

inline IrOp pause() {
  IrOp op;
  op.kind = IrKind::Stop;
  op.opcode = 0x50;
  op.semantic = "pause";
  op.meta.mnemonic = "\u0421/\u041f";
  op.meta.comment = "pause";
  return op;
}

inline IrOp ret() {
  IrOp op;
  op.kind = IrKind::Return;
  op.opcode = 0x52;
  op.meta.mnemonic = "\u0412/\u041e";
  return op;
}

inline mkpro::CompileOptions noop_options() {
  mkpro::CompileOptions options;
  options.delivery = mkpro::DeliveryMode::Manual;
  options.budget = 105;
  options.analysis = false;
  return options;
}

inline void require_applied(int actual, int expected, const std::string& label) {
  require(actual == expected,
          label + ": expected applied=" + std::to_string(expected) + ", got " +
              std::to_string(actual));
}

inline void require_ops_equal(const std::vector<IrOp>& actual, const std::vector<IrOp>& expected,
                              const std::string& label) {
  const std::string actual_json = mkpro::ir_ops_to_json(actual);
  const std::string expected_json = mkpro::ir_ops_to_json(expected);
  require(actual_json == expected_json,
          label + ": ops mismatch\n  expected " + expected_json + "\n  actual   " + actual_json);
}

} // namespace mkpro::tests::irbuild
