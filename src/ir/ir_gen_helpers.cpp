#include <cstring>
#include <stdexcept>

#include "ir/ir_gen.hpp"

namespace ether::ir_gen {

void IRGenerator::emit_int64(int64_t val) {
    uint8_t bytes[8];
    std::memcpy(bytes, &val, 8);
    for (int i = 0; i < 8; ++i) emit_byte(bytes[i]);
}

void IRGenerator::emit_int32(int32_t val) {
    uint8_t bytes[4];
    std::memcpy(bytes, &val, 4);
    for (int i = 0; i < 4; ++i) emit_byte(bytes[i]);
}

void IRGenerator::emit_int16(int16_t val) {
    uint8_t bytes[2];
    std::memcpy(bytes, &val, 2);
    for (int i = 0; i < 2; ++i) emit_byte(bytes[i]);
}

void IRGenerator::emit_int8(int8_t val) { emit_byte(static_cast<uint8_t>(val)); }

void IRGenerator::emit_uint32(uint32_t val) {
    uint8_t bytes[4];
    std::memcpy(bytes, &val, 4);
    for (int i = 0; i < 4; ++i) emit_byte(bytes[i]);
}

void IRGenerator::emit_uint16(uint16_t val) {
    uint8_t bytes[2];
    std::memcpy(bytes, &val, 2);
    for (int i = 0; i < 2; ++i) emit_byte(bytes[i]);
}

uint32_t IRGenerator::get_string_id(const std::string &str) {
    for (uint32_t i = 0; i < m_program.string_pool.size(); ++i) {
        if (m_program.string_pool[i] == str) return i;
    }
    m_program.string_pool.push_back(str);
    return (uint32_t)m_program.string_pool.size() - 1;
}

IRGenerator::Symbol IRGenerator::get_var_symbol(const std::string &name) {
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
        if (it->variables.contains(name)) return it->variables.at(name);
    }
    throw std::runtime_error("Undefined variable: " + name);
}

void IRGenerator::define_var(const std::string &name, uint16_t size) {
    if (m_scopes.empty()) throw std::runtime_error("Variable defined outside scope");
    auto &scope = m_scopes.back();
    uint16_t slot = scope.next_slot;
    scope.next_slot += size;
    scope.variables[name] = {slot, (uint8_t)size, scope.is_global};
}

IRGenerator::JumpPlaceholder IRGenerator::emit_jump(ir::OpCode op, uint32_t target) {
    emit_opcode(op);
    size_t pos = m_program.bytecode.size();
    emit_uint32(target);
    return {pos};
}

void IRGenerator::patch_jump(JumpPlaceholder jp, uint32_t target) {
    std::memcpy(&m_program.bytecode[jp.pos], &target, 4);
}

uint32_t IRGenerator::get_type_size(const parser::DataType &type) {
    uint32_t num_slots = 1;
    if (type.kind == parser::DataType::Kind::Struct) {
        num_slots = m_structs.at(type.struct_name).total_size;
    }
    return num_slots * 16;
}

void IRGenerator::emit_push_i64(int64_t val) {
    emit_opcode(ir::OpCode::PUSH_I64);
    emit_int64(val);
}

void IRGenerator::emit_push_i32(int32_t val) {
    emit_opcode(ir::OpCode::PUSH_I32);
    emit_int32(val);
}

void IRGenerator::emit_push_i16(int16_t val) {
    emit_opcode(ir::OpCode::PUSH_I16);
    emit_int16(val);
}

void IRGenerator::emit_push_i8(int8_t val) {
    emit_opcode(ir::OpCode::PUSH_I8);
    emit_int8(val);
}

void IRGenerator::emit_push_str(uint32_t id) {
    emit_opcode(ir::OpCode::PUSH_STR);
    emit_uint32(id);
}

void IRGenerator::emit_load_var(uint16_t slot, uint8_t size) {
    emit_opcode(ir::OpCode::LOAD_VAR);
    emit_uint16(slot);
    emit_byte(size);
}

void IRGenerator::emit_store_var(uint16_t slot, uint8_t size) {
    emit_opcode(ir::OpCode::STORE_VAR);
    emit_uint16(slot);
    emit_byte(size);
}

void IRGenerator::emit_load_global(uint16_t slot, uint8_t size) {
    emit_opcode(ir::OpCode::LOAD_GLOBAL);
    emit_uint16(slot);
    emit_byte(size);
}

void IRGenerator::emit_store_global(uint16_t slot, uint8_t size) {
    emit_opcode(ir::OpCode::STORE_GLOBAL);
    emit_uint16(slot);
    emit_byte(size);
}

void IRGenerator::emit_add() { emit_opcode(ir::OpCode::ADD); }
void IRGenerator::emit_sub() { emit_opcode(ir::OpCode::SUB); }
void IRGenerator::emit_mul() { emit_opcode(ir::OpCode::MUL); }
void IRGenerator::emit_div() { emit_opcode(ir::OpCode::DIV); }

void IRGenerator::emit_ret(uint8_t size) {
    emit_opcode(ir::OpCode::RET);
    emit_byte(size);
}

void IRGenerator::emit_halt() { emit_opcode(ir::OpCode::HALT); }

void IRGenerator::emit_syscall(uint8_t args) {
    emit_opcode(ir::OpCode::SYSCALL);
    emit_byte(args);
}

void IRGenerator::emit_call(uint32_t addr, uint8_t args) {
    emit_opcode(ir::OpCode::CALL);
    emit_uint32(addr);
    emit_byte(args);
}

void IRGenerator::emit_spawn(uint32_t addr, uint8_t args) {
    emit_opcode(ir::OpCode::SPAWN);
    emit_uint32(addr);
    emit_byte(args);
}

void IRGenerator::emit_lea_stack(uint16_t slot) {
    emit_opcode(ir::OpCode::LEA_STACK);
    emit_uint16(slot);
}

void IRGenerator::emit_lea_global(uint16_t slot) {
    emit_opcode(ir::OpCode::LEA_GLOBAL);
    emit_uint16(slot);
}

void IRGenerator::emit_load_ptr_offset(int32_t offset, uint8_t size) {
    emit_opcode(ir::OpCode::LOAD_PTR_OFFSET);
    emit_int32(offset);
    emit_byte(size);
}

void IRGenerator::emit_store_ptr_offset(int32_t offset, uint8_t size) {
    emit_opcode(ir::OpCode::STORE_PTR_OFFSET);
    emit_int32(offset);
    emit_byte(size);
}

void IRGenerator::emit_push_varargs() { emit_opcode(ir::OpCode::PUSH_VARARGS); }
void IRGenerator::emit_pop() { emit_opcode(ir::OpCode::POP); }
void IRGenerator::emit_yield() { emit_opcode(ir::OpCode::YIELD); }
void IRGenerator::emit_await() { emit_opcode(ir::OpCode::AWAIT); }

void IRGenerator::emit_eq() { emit_opcode(ir::OpCode::CMP_EQ); }
void IRGenerator::emit_le() { emit_opcode(ir::OpCode::CMP_LE); }
void IRGenerator::emit_lt() { emit_opcode(ir::OpCode::CMP_LT); }
void IRGenerator::emit_gt() { emit_opcode(ir::OpCode::CMP_GT); }
void IRGenerator::emit_ge() { emit_opcode(ir::OpCode::CMP_GE); }

}  // namespace ether::ir_gen
