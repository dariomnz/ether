#ifndef ETHER_IR_GEN_HPP
#define ETHER_IR_GEN_HPP

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ir.hpp"
#include "parser/ast.hpp"

namespace ether::ir_gen {

class IRGenerator : public parser::ConstASTVisitor {
    struct LValueResolver : parser::DefaultIgnoreConstASTVisitor {
        friend class IRGenerator;
        IRGenerator *gen;
        enum Kind { Stack, Heap } kind = Stack;
        uint16_t slot = 0;
        bool is_global = false;
        uint8_t offset = 0;
        void visit(const parser::VariableExpression &v) override;
        void visit(const parser::MemberAccessExpression &m) override;
        void visit(const parser::IndexExpression &idx) override;
    };

   public:
    ir::IRProgram generate(const parser::Program &ast);

    void visit(const parser::IntegerLiteral &node) override;
    void visit(const parser::FloatLiteral &node) override;
    void visit(const parser::StringLiteral &node) override;
    void visit(const parser::VariableExpression &node) override;
    void visit(const parser::FunctionCall &node) override;
    void visit(const parser::VarargExpression &node) override;
    void visit(const parser::BinaryExpression &node) override;
    void visit(const parser::Block &node) override;
    void visit(const parser::IfStatement &node) override;
    void visit(const parser::ReturnStatement &node) override;
    void visit(const parser::ExpressionStatement &node) override;
    void visit(const parser::YieldStatement &node) override;
    void visit(const parser::SpawnExpression &node) override;
    void visit(const parser::AssignmentExpression &node) override;
    void visit(const parser::IncrementExpression &node) override;
    void visit(const parser::DecrementExpression &node) override;
    void visit(const parser::AwaitExpression &node) override;
    void visit(const parser::ForStatement &node) override;
    void visit(const parser::VariableDeclaration &node) override;
    void visit(const parser::Function &node) override;
    void visit(const parser::StructDeclaration &node) override;
    void visit(const parser::Program &node) override;
    void visit(const parser::MemberAccessExpression &node) override;
    void visit(const parser::IndexExpression &node) override;
    void visit(const parser::Include &node) override;

   private:
    ir::IRProgram m_program;
    std::unordered_set<std::string> m_reachable;

    // Tracking for bytecode generation
    struct Symbol {
        uint16_t slot;
        uint8_t size = 1;
        bool is_global = false;
    };
    struct Scope {
        std::unordered_map<std::string, Symbol> variables;
        uint16_t next_slot = 0;
        bool is_global = false;
    };
    std::vector<Scope> m_scopes;

    struct StructInfo {
        std::unordered_map<std::string, uint8_t> member_offsets;
        uint16_t total_size;
    };
    std::unordered_map<std::string, StructInfo> m_structs;

    // Helpers
    void emit_byte(uint8_t byte) { m_program.bytecode.push_back(byte); }
    void emit_opcode(ir::OpCode op) { emit_byte(static_cast<uint8_t>(op)); }
    void emit_int64(int64_t val);
    void emit_int32(int32_t val);
    void emit_int16(int16_t val);
    void emit_int8(int8_t val);
    void emit_uint32(uint32_t val);
    void emit_uint16(uint16_t val);

    // Context-unaware helpers (OpCode emission)
    void emit_push_i64(int64_t val);
    void emit_push_i32(int32_t val);
    void emit_push_i16(int16_t val);
    void emit_push_i8(int8_t val);
    void emit_push_f64(double val);
    void emit_push_f32(float val);
    void emit_push_str(uint32_t id);
    void emit_load_var(uint16_t slot, uint8_t size = 1);
    void emit_store_var(uint16_t slot, uint8_t size = 1);
    void emit_load_global(uint16_t slot, uint8_t size = 1);
    void emit_store_global(uint16_t slot, uint8_t size = 1);
    void emit_add();
    void emit_sub();
    void emit_mul();
    void emit_div();
    void emit_add_f();
    void emit_sub_f();
    void emit_mul_f();
    void emit_div_f();
    void emit_ret(uint8_t size = 1);
    void emit_halt();
    void emit_syscall(uint8_t args);
    void emit_call(uint32_t addr, uint8_t args);
    void emit_spawn(uint32_t addr, uint8_t args);
    void emit_lea_stack(uint16_t slot);
    void emit_lea_global(uint16_t slot);
    void emit_load_ptr_offset(int32_t offset, uint8_t size = 1);
    void emit_store_ptr_offset(int32_t offset, uint8_t size = 1);
    void emit_push_varargs();
    void emit_pop();
    void emit_yield();
    void emit_await();
    void emit_eq();
    void emit_le();
    void emit_lt();
    void emit_gt();
    void emit_ge();
    void emit_eq_f();
    void emit_le_f();
    void emit_lt_f();
    void emit_gt_f();
    void emit_ge_f();

    uint32_t get_string_id(const std::string &str);
    Symbol get_var_symbol(const std::string &name);
    uint32_t get_type_size(const parser::DataType &type);
    void visit(const parser::SizeofExpression &node) override;
    void define_var(const std::string &name, uint16_t size = 1);

    struct JumpPlaceholder {
        size_t pos;
    };
    JumpPlaceholder emit_jump(ir::OpCode op, uint32_t target = 0);
    void patch_jump(JumpPlaceholder jp, uint32_t target);

    struct CallPatch {
        size_t pos;
        std::string func_name;
    };
    std::vector<CallPatch> m_call_patches;
};

}  // namespace ether::ir_gen

#endif  // ETHER_IR_GEN_HPP
