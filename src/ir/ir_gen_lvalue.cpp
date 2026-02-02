#include "ir/ir_gen.hpp"

namespace ether::ir_gen {

void IRGenerator::LValueResolver::visit(const parser::VariableExpression &v) {
    auto s = gen->get_var_symbol(v.name);
    kind = Stack;
    slot = s.slot;
    is_global = s.is_global;
}

void IRGenerator::LValueResolver::visit(const parser::MemberAccessExpression &m) {
    m.object->accept(*this);
    std::string struct_name;
    bool is_ptr = false;
    if (m.object->type->kind == parser::DataType::Kind::Ptr) {
        struct_name = m.object->type->inner->struct_name;
        is_ptr = true;
    } else {
        struct_name = m.object->type->struct_name;
    }
    uint8_t m_offset = gen->m_structs.at(struct_name).member_offsets.at(m.member_name);

    if (kind == Stack) {
        if (is_ptr) {
            if (is_global) {
                gen->emit_load_global(slot, 1);
            } else {
                gen->emit_load_var(slot, 1);
            }
            kind = Heap;
            offset = m_offset;
        } else {
            slot += m_offset;
        }
    } else {
        if (is_ptr) {
            gen->emit_load_ptr_offset(offset, 1);
            offset = m_offset;
        } else {
            offset += m_offset;
        }
    }
}

void IRGenerator::LValueResolver::visit(const parser::IndexExpression &idx) {
    // Load pointer
    idx.object->accept(*this);

    // If we're in Stack mode, we need to get the address
    if (kind == Stack) {
        // Load the pointer value (arrays are pointer-like)
        if (is_global) {
            gen->emit_load_global(slot, 1);
        } else {
            gen->emit_load_var(slot, 1);
        }
    } else {
        // Already have a pointer on stack from previous operations
        // Only load if we have a non-zero offset (nested member access)
        if (offset != 0) {
            gen->emit_load_ptr_offset(offset, 1);
        }
    }

    // Now compute index offset
    idx.index->accept(*gen);

    // Multiply by element size if needed
    uint16_t element_size = 1;
    if (idx.object->type) {
        if (idx.object->type->kind == parser::DataType::Kind::Ptr && idx.object->type->inner) {
            if (idx.object->type->inner->kind == parser::DataType::Kind::Struct) {
                element_size = gen->m_structs.at(idx.object->type->inner->struct_name).total_size;
            }
        } else if (idx.object->type->kind == parser::DataType::Kind::Array && idx.object->type->inner) {
            if (idx.object->type->inner->kind == parser::DataType::Kind::Struct) {
                element_size = gen->m_structs.at(idx.object->type->inner->struct_name).total_size;
            }
        }
    }

    // Multiply by 16 to convert slot offset to byte offset
    gen->emit_push_i32(16 * element_size);  // sizeof(Value)
    gen->emit_mul();

    // Add to get final address
    gen->emit_add();

    // Now we're in Heap mode with computed address on stack
    kind = Heap;
    offset = 0;
}

}  // namespace ether::ir_gen
