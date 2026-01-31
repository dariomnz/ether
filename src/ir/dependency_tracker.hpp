#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "parser/ast.hpp"

namespace ether::ir_gen {

struct DependencyTracker : public parser::ConstASTVisitor {
    std::unordered_set<std::string> reachable;
    const std::unordered_map<std::string, const parser::Function *> &all_funcs;
    const std::unordered_map<std::string, const parser::VariableDeclaration *> &all_globals;

    DependencyTracker(const std::unordered_map<std::string, const parser::Function *> &funcs,
                      const std::unordered_map<std::string, const parser::VariableDeclaration *> &globals)
        : all_funcs(funcs), all_globals(globals) {}

    void trace(const std::string &name) {
        if (name == "syscall") return;
        if (reachable.contains(name)) return;
        reachable.insert(name);

        auto it_f = all_funcs.find(name);
        if (it_f != all_funcs.end()) {
            it_f->second->accept(*this);
        }

        auto it_g = all_globals.find(name);
        if (it_g != all_globals.end()) {
            if (it_g->second->init) {
                it_g->second->init->accept(*this);
            }
        }
    }

    void visit(const parser::IntegerLiteral &) override {}
    void visit(const parser::FloatLiteral &) override {}
    void visit(const parser::StringLiteral &) override {}
    void visit(const parser::VariableExpression &node) override { trace(node.name); }
    void visit(const parser::FunctionCall &node) override {
        trace(node.name);
        for (const auto &arg : node.args) arg->accept(*this);
    }
    void visit(const parser::VarargExpression &) override {}
    void visit(const parser::BinaryExpression &node) override {
        if (node.left) node.left->accept(*this);
        if (node.right) node.right->accept(*this);
    }
    void visit(const parser::Block &node) override {
        for (const auto &stmt : node.statements) stmt->accept(*this);
    }
    void visit(const parser::IfStatement &node) override {
        if (node.condition) node.condition->accept(*this);
        if (node.then_branch) node.then_branch->accept(*this);
        if (node.else_branch) node.else_branch->accept(*this);
    }
    void visit(const parser::ReturnStatement &node) override {
        if (node.expr) node.expr->accept(*this);
    }
    void visit(const parser::ExpressionStatement &node) override {
        if (node.expr) node.expr->accept(*this);
    }
    void visit(const parser::YieldStatement &) override {}
    void visit(const parser::SpawnExpression &node) override {
        if (node.call) node.call->accept(*this);
    }
    void visit(const parser::AssignmentExpression &node) override {
        node.lvalue->accept(*this);
        if (node.value) node.value->accept(*this);
    }
    void visit(const parser::IncrementExpression &node) override {
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(const parser::DecrementExpression &node) override {
        if (node.lvalue) node.lvalue->accept(*this);
    }
    void visit(const parser::MemberAccessExpression &node) override {
        if (node.object) node.object->accept(*this);
    }
    void visit(const parser::IndexExpression &node) override {
        if (node.object) node.object->accept(*this);
        if (node.index) node.index->accept(*this);
    }
    void visit(const parser::StructDeclaration &node) override {}
    void visit(const parser::AwaitExpression &node) override {
        if (node.expr) node.expr->accept(*this);
    }
    void visit(const parser::SizeofExpression &) override {}
    void visit(const parser::ForStatement &node) override {
        if (node.init) node.init->accept(*this);
        if (node.condition) node.condition->accept(*this);
        if (node.increment) node.increment->accept(*this);
        if (node.body) node.body->accept(*this);
    }
    void visit(const parser::VariableDeclaration &node) override {
        if (node.init) node.init->accept(*this);
    }
    void visit(const parser::Function &node) override {
        if (node.body) node.body->accept(*this);
    }
    void visit(const parser::Include &) override {}
    void visit(const parser::Program &) override {}
};

}  // namespace ether::ir_gen
