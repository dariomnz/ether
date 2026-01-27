#ifndef ETHER_AST_HPP
#define ETHER_AST_HPP

#include <memory>
#include <string_view>
#include <vector>

namespace ether::parser {

struct ASTNode {
    virtual ~ASTNode() = default;
};

struct Expression : ASTNode {};
struct Statement : ASTNode {};

struct IntegerLiteral : Expression {
    int value;
    explicit IntegerLiteral(int val) : value(val) {}
};

struct StringLiteral : Expression {
    std::string value;
    explicit StringLiteral(std::string v) : value(std::move(v)) {}
};

struct VariableExpression : Expression {
    std::string name;
    explicit VariableExpression(std::string n) : name(std::move(n)) {}
};

struct FunctionCall : Expression {
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a) : name(std::move(n)), args(std::move(a)) {}
};

struct BinaryExpression : Expression {
    enum class Op { Add, Sub, Mul, Div, Eq, Leq, Less, Gt, Geq };
    Op op;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;

    BinaryExpression(Op o, std::unique_ptr<Expression> l, std::unique_ptr<Expression> r)
        : op(o), left(std::move(l)), right(std::move(r)) {}
};

struct Block : Statement {
    std::vector<std::unique_ptr<Statement>> statements;
};

struct IfStatement : Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> then_branch;
    std::unique_ptr<Block> else_branch;  // optional
    IfStatement(std::unique_ptr<Expression> cond, std::unique_ptr<Block> tb, std::unique_ptr<Block> eb = nullptr)
        : condition(std::move(cond)), then_branch(std::move(tb)), else_branch(std::move(eb)) {}
};

struct ReturnStatement : Statement {
    std::unique_ptr<Expression> expr;
    explicit ReturnStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
};

struct ExpressionStatement : Statement {
    std::unique_ptr<Expression> expr;
    explicit ExpressionStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
};

struct AssignmentExpression : Expression {
    std::string name;
    std::unique_ptr<Expression> value;
    AssignmentExpression(std::string n, std::unique_ptr<Expression> v) : name(std::move(n)), value(std::move(v)) {}
};

struct IncrementExpression : Expression {
    std::string name;
    explicit IncrementExpression(std::string n) : name(std::move(n)) {}
};

struct ForStatement : Statement {
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    std::unique_ptr<Block> body;
    ForStatement(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc,
                 std::unique_ptr<Block> b)
        : init(std::move(i)), condition(std::move(c)), increment(std::move(inc)), body(std::move(b)) {}
};

struct VariableDeclaration : Statement {
    std::string name;
    std::unique_ptr<Expression> init;
    VariableDeclaration(std::string n, std::unique_ptr<Expression> i) : name(std::move(n)), init(std::move(i)) {}
};

struct Function : Expression {
    std::string name;
    std::vector<std::string> params;
    std::unique_ptr<Block> body;
    Function(std::string n, std::vector<std::string> p, std::unique_ptr<Block> b)
        : name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
};

struct Program : ASTNode {
    std::vector<std::unique_ptr<Function>> functions;
};

}  // namespace ether::parser

#endif  // ETHER_AST_HPP
