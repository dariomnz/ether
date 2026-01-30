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
    scope.variables[name] = {slot, scope.is_global};
}

IRGenerator::JumpPlaceholder IRGenerator::emit_jump(ir::OpCode op) {
    emit_opcode(op);
    size_t pos = m_program.bytecode.size();
    emit_uint32(0);  // placeholder
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

}  // namespace ether::ir_gen
