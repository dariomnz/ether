#include "ir_gen.hpp"

#include <cstring>

#include "ir/dependency_tracker.hpp"


namespace ether::ir_gen {

ir::IRProgram IRGenerator::generate(const parser::Program &ast) {
    m_program.bytecode.clear();
    m_program.string_pool.clear();
    m_program.functions.clear();
    m_program.addr_to_info.clear();
    m_call_patches.clear();
    m_reachable.clear();
    m_scopes.clear();

    // 1. Dependency Tracking
    std::unordered_map<std::string, const parser::Function *> all_funcs;
    for (const auto &func : ast.functions) {
        all_funcs[func->name] = func.get();
    }
    std::unordered_map<std::string, const parser::VariableDeclaration *> all_globals_map;
    for (const auto &global : ast.globals) {
        all_globals_map[global->name] = global.get();
    }

    DependencyTracker tracker(all_funcs, all_globals_map);
    tracker.trace("main");
    m_reachable = std::move(tracker.reachable);

    // 2. Global Scope setup
    m_scopes.push_back({{}, 0, true});

    // Define ONLY reachable globals
    for (const auto &global : ast.globals) {
        if (m_reachable.contains(global->name)) {
            uint16_t size = 1;
            if (global->type.kind == parser::DataType::Kind::Struct) {
                size = m_structs.at(global->type.struct_name).total_size;
            }
            define_var(global->name, size);
        }
    }
    m_program.num_globals = m_scopes[0].next_slot;

    for (const auto &name : m_reachable) {
        auto it = all_funcs.find(name);
        if (it != all_funcs.end()) {
            const auto *func = it->second;
            m_program.functions[name] = {0, (uint8_t)func->params.size(), 0};
        }
    }
    m_program.functions["syscall"] = {0xFFFFFFFF, 0, 0};

    // 3. Collect struct layouts
    for (const auto &str : ast.structs) {
        StructInfo info;
        uint16_t offset = 0;
        for (const auto &member : str->members) {
            info.member_offsets[member.name] = (uint8_t)offset;
            uint16_t member_size = 1;
            if (member.type.kind == parser::DataType::Kind::Struct) {
                auto it = m_structs.find(member.type.struct_name);
                if (it != m_structs.end()) {
                    member_size = it->second.total_size;
                }
            }
            offset += member_size;
        }
        info.total_size = offset;
        m_structs[str->name] = info;
    }

    // 4. Entry point / Global initialization
    m_program.main_addr = 0;
    for (const auto &global : ast.globals) {
        if (m_reachable.contains(global->name) && global->init) {
            global->init->accept(*this);
            Symbol s = get_var_symbol(global->name);
            emit_opcode(ir::OpCode::STORE_GLOBAL);
            emit_uint16(s.slot);
        }
    }

    // Call main and halt
    emit_opcode(ir::OpCode::CALL);
    m_call_patches.push_back({m_program.bytecode.size(), "main"});
    emit_uint32(0);
    emit_byte(0);  // main takes 0 args
    emit_opcode(ir::OpCode::HALT);

    // 5. Generate functions
    ast.accept(*this);

    emit_opcode(ir::OpCode::HALT);

    for (const auto &patch : m_call_patches) {
        uint32_t addr = (uint32_t)m_program.functions.at(patch.func_name).entry_addr;
        std::memcpy(&m_program.bytecode[patch.pos], &addr, 4);
    }

    for (const auto &[name, info] : m_program.functions) {
        m_program.addr_to_info[info.entry_addr] = info;
    }

    return std::move(m_program);
}

void IRGenerator::visit(const parser::Program &node) {
    for (const auto &func : node.functions) {
        if (!m_reachable.contains(func->name)) continue;
        m_program.functions[func->name].entry_addr = m_program.bytecode.size();
        func->accept(*this);
    }
    for (const auto &str : node.structs) {
        str->accept(*this);
    }
}

}  // namespace ether::ir_gen
