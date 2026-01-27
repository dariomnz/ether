#ifndef ETHER_AST_HPP
#define ETHER_AST_HPP

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ether::parser {

struct ASTNode {
    std::string filename;
    int line;
    int column;
    ASTNode(std::string fn, int l, int c) : filename(std::move(fn)), line(l), column(c) {}
    virtual ~ASTNode() = default;
};

struct DataType {
    enum class Kind { Int, Coroutine, Void };
    Kind kind;
    DataType() : kind(Kind::Int) {}
    explicit DataType(Kind k) : kind(k) {}
    bool operator==(const DataType &other) const { return kind == other.kind; }
};

struct Expression : ASTNode {
    std::unique_ptr<DataType> type;  // Assigned during semantic analysis
    Expression(std::string fn, int l, int c) : ASTNode(std::move(fn), l, c) {}
};
struct Statement : ASTNode {
    Statement(std::string fn, int l, int c) : ASTNode(std::move(fn), l, c) {}
};

struct IntegerLiteral : Expression {
    int value;
    IntegerLiteral(int val, std::string fn, int l, int c) : Expression(std::move(fn), l, c), value(val) {}
};

struct StringLiteral : Expression {
    std::string value;
    StringLiteral(std::string v, std::string fn, int l, int c) : Expression(std::move(fn), l, c), value(std::move(v)) {}
};

struct VariableExpression : Expression {
    std::string name;
    VariableExpression(std::string n, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), name(std::move(n)) {}
};

struct FunctionCall : Expression {
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), name(std::move(n)), args(std::move(a)) {}
};

struct BinaryExpression : Expression {
    enum class Op { Add, Sub, Mul, Div, Eq, Leq, Less, Gt, Geq };
    Op op;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;

    BinaryExpression(Op o, std::unique_ptr<Expression> l, std::unique_ptr<Expression> r, std::string fn, int line,
                     int c)
        : Expression(std::move(fn), line, c), op(o), left(std::move(l)), right(std::move(r)) {}
};

struct Block : Statement {
    std::vector<std::unique_ptr<Statement>> statements;
    Block(std::string fn, int l, int c) : Statement(std::move(fn), l, c) {}
};

struct IfStatement : Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> then_branch;
    std::unique_ptr<Block> else_branch;  // optional
    IfStatement(std::unique_ptr<Expression> cond, std::unique_ptr<Block> tb, std::unique_ptr<Block> eb, std::string fn,
                int l, int c)
        : Statement(std::move(fn), l, c),
          condition(std::move(cond)),
          then_branch(std::move(tb)),
          else_branch(std::move(eb)) {}
};

struct ReturnStatement : Statement {
    std::unique_ptr<Expression> expr;
    ReturnStatement(std::unique_ptr<Expression> e, std::string fn, int l, int c)
        : Statement(std::move(fn), l, c), expr(std::move(e)) {}
};

struct ExpressionStatement : Statement {
    std::unique_ptr<Expression> expr;
    ExpressionStatement(std::unique_ptr<Expression> e, std::string fn, int l, int c)
        : Statement(std::move(fn), l, c), expr(std::move(e)) {}
};

struct YieldStatement : Statement {
    YieldStatement(std::string fn, int l, int c) : Statement(std::move(fn), l, c) {}
};

struct SpawnExpression : Expression {
    std::unique_ptr<FunctionCall> call;
    SpawnExpression(std::unique_ptr<FunctionCall> c, std::string fn, int l, int c_pos)
        : Expression(std::move(fn), l, c_pos), call(std::move(c)) {}
};

struct AssignmentExpression : Expression {
    std::string name;
    std::unique_ptr<Expression> value;
    AssignmentExpression(std::string n, std::unique_ptr<Expression> v, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), name(std::move(n)), value(std::move(v)) {}
};

struct IncrementExpression : Expression {
    std::string name;
    IncrementExpression(std::string n, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), name(std::move(n)) {}
};

struct DecrementExpression : Expression {
    std::string name;
    DecrementExpression(std::string n, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), name(std::move(n)) {}
};

struct AwaitExpression : Expression {
    std::unique_ptr<Expression> expr;
    AwaitExpression(std::unique_ptr<Expression> e, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), expr(std::move(e)) {}
};

struct ForStatement : Statement {
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    std::unique_ptr<Block> body;
    ForStatement(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc,
                 std::unique_ptr<Block> b, std::string fn, int l, int c_pos)
        : Statement(std::move(fn), l, c_pos),
          init(std::move(i)),
          condition(std::move(c)),
          increment(std::move(inc)),
          body(std::move(b)) {}
};

struct VariableDeclaration : Statement {
    DataType type;
    std::string name;
    std::unique_ptr<Expression> init;
    VariableDeclaration(DataType t, std::string n, std::unique_ptr<Expression> i, std::string fn, int l, int c)
        : Statement(std::move(fn), l, c), type(t), name(std::move(n)), init(std::move(i)) {}
};

struct Parameter {
    DataType type;
    std::string name;
};

struct Function : Expression {
    DataType return_type;
    std::string name;
    std::vector<Parameter> params;
    std::unique_ptr<Block> body;
    Function(DataType rt, std::string n, std::vector<Parameter> p, std::unique_ptr<Block> b, std::string fn, int l,
             int c)
        : Expression(std::move(fn), l, c),
          return_type(rt),
          name(std::move(n)),
          params(std::move(p)),
          body(std::move(b)) {}
};

struct Program : ASTNode {
    std::vector<std::unique_ptr<Function>> functions;
    Program() : ASTNode("", 0, 0) {}
};

}  // namespace ether::parser

#endif  // ETHER_AST_HPP
