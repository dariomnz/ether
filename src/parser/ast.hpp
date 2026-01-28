#ifndef ETHER_AST_HPP
#define ETHER_AST_HPP

#include <memory>
#include <string>
#include <vector>

namespace ether::parser {
struct IntegerLiteral;
struct StringLiteral;
struct VariableExpression;
struct FunctionCall;
struct BinaryExpression;
struct Block;
struct IfStatement;
struct ReturnStatement;
struct ExpressionStatement;
struct YieldStatement;
struct SpawnExpression;
struct AssignmentExpression;
struct VarargExpression;
struct IncrementExpression;
struct DecrementExpression;
struct AwaitExpression;
struct ForStatement;
struct VariableDeclaration;
struct Function;
struct Include;
struct Program;

struct ASTVisitor {
    virtual ~ASTVisitor() = default;
    virtual void visit(IntegerLiteral& node) { visit(static_cast<const IntegerLiteral&>(node)); }
    virtual void visit(const IntegerLiteral& node) {}
    virtual void visit(StringLiteral& node) { visit(static_cast<const StringLiteral&>(node)); }
    virtual void visit(const StringLiteral& node) {}
    virtual void visit(VariableExpression& node) { visit(static_cast<const VariableExpression&>(node)); }
    virtual void visit(const VariableExpression& node) {}
    virtual void visit(FunctionCall& node) { visit(static_cast<const FunctionCall&>(node)); }
    virtual void visit(const FunctionCall& node) {}
    virtual void visit(VarargExpression& node) { visit(static_cast<const VarargExpression&>(node)); }
    virtual void visit(const VarargExpression& node) {}
    virtual void visit(BinaryExpression& node) { visit(static_cast<const BinaryExpression&>(node)); }
    virtual void visit(const BinaryExpression& node) {}
    virtual void visit(Block& node) { visit(static_cast<const Block&>(node)); }
    virtual void visit(const Block& node) {}
    virtual void visit(IfStatement& node) { visit(static_cast<const IfStatement&>(node)); }
    virtual void visit(const IfStatement& node) {}
    virtual void visit(ReturnStatement& node) { visit(static_cast<const ReturnStatement&>(node)); }
    virtual void visit(const ReturnStatement& node) {}
    virtual void visit(ExpressionStatement& node) { visit(static_cast<const ExpressionStatement&>(node)); }
    virtual void visit(const ExpressionStatement& node) {}
    virtual void visit(YieldStatement& node) { visit(static_cast<const YieldStatement&>(node)); }
    virtual void visit(const YieldStatement& node) {}
    virtual void visit(SpawnExpression& node) { visit(static_cast<const SpawnExpression&>(node)); }
    virtual void visit(const SpawnExpression& node) {}
    virtual void visit(AssignmentExpression& node) { visit(static_cast<const AssignmentExpression&>(node)); }
    virtual void visit(const AssignmentExpression& node) {}
    virtual void visit(IncrementExpression& node) { visit(static_cast<const IncrementExpression&>(node)); }
    virtual void visit(const IncrementExpression& node) {}
    virtual void visit(DecrementExpression& node) { visit(static_cast<const DecrementExpression&>(node)); }
    virtual void visit(const DecrementExpression& node) {}
    virtual void visit(AwaitExpression& node) { visit(static_cast<const AwaitExpression&>(node)); }
    virtual void visit(const AwaitExpression& node) {}
    virtual void visit(ForStatement& node) { visit(static_cast<const ForStatement&>(node)); }
    virtual void visit(const ForStatement& node) {}
    virtual void visit(VariableDeclaration& node) { visit(static_cast<const VariableDeclaration&>(node)); }
    virtual void visit(const VariableDeclaration& node) {}
    virtual void visit(Function& node) { visit(static_cast<const Function&>(node)); }
    virtual void visit(const Function& node) {}
    virtual void visit(Include& node) { visit(static_cast<const Include&>(node)); }
    virtual void visit(const Include& node) {}
    virtual void visit(Program& node) { visit(static_cast<const Program&>(node)); }
    virtual void visit(const Program& node) {}
};

struct ASTNode {
    std::string filename;
    int line;
    int column;
    ASTNode(std::string fn, int l, int c) : filename(std::move(fn)), line(l), column(c) {}
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;
    virtual void accept(ASTVisitor& visitor) const = 0;
};

struct DataType {
    enum class Kind { Int, Coroutine, Void, Ptr };
    Kind kind;
    DataType() : kind(Kind::Int) {}
    explicit DataType(Kind k) : kind(k) {}
    bool operator==(const DataType& other) const { return kind == other.kind; }
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
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct StringLiteral : Expression {
    std::string value;
    StringLiteral(std::string v, std::string fn, int l, int c) : Expression(std::move(fn), l, c), value(std::move(v)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct VariableExpression : Expression {
    std::string name;
    std::string decl_filename;
    int decl_line = 0;
    int decl_col = 0;
    VariableExpression(std::string n, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), name(std::move(n)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct FunctionCall : Expression {
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    std::string decl_filename;
    int decl_line = 0;
    int decl_col = 0;
    std::vector<DataType> param_types;
    bool is_variadic = false;
    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), name(std::move(n)), args(std::move(a)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct VarargExpression : Expression {
    VarargExpression(std::string fn, int l, int c) : Expression(std::move(fn), l, c) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct BinaryExpression : Expression {
    enum class Op { Add, Sub, Mul, Div, Eq, Leq, Less, Gt, Geq };
    Op op;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;

    BinaryExpression(Op o, std::unique_ptr<Expression> l, std::unique_ptr<Expression> r, std::string fn, int line,
                     int c)
        : Expression(std::move(fn), line, c), op(o), left(std::move(l)), right(std::move(r)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct Block : Statement {
    std::vector<std::unique_ptr<Statement>> statements;
    Block(std::string fn, int l, int c) : Statement(std::move(fn), l, c) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
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
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ReturnStatement : Statement {
    std::unique_ptr<Expression> expr;
    ReturnStatement(std::unique_ptr<Expression> e, std::string fn, int l, int c)
        : Statement(std::move(fn), l, c), expr(std::move(e)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ExpressionStatement : Statement {
    std::unique_ptr<Expression> expr;
    ExpressionStatement(std::unique_ptr<Expression> e, std::string fn, int l, int c)
        : Statement(std::move(fn), l, c), expr(std::move(e)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct YieldStatement : Statement {
    YieldStatement(std::string fn, int l, int c) : Statement(std::move(fn), l, c) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct SpawnExpression : Expression {
    std::unique_ptr<FunctionCall> call;
    SpawnExpression(std::unique_ptr<FunctionCall> c, std::string fn, int l, int c_pos)
        : Expression(std::move(fn), l, c_pos), call(std::move(c)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct AssignmentExpression : Expression {
    std::unique_ptr<VariableExpression> lvalue;
    std::unique_ptr<Expression> value;
    AssignmentExpression(std::unique_ptr<VariableExpression> lv, std::unique_ptr<Expression> v, std::string fn, int l,
                         int c)
        : Expression(std::move(fn), l, c), lvalue(std::move(lv)), value(std::move(v)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct IncrementExpression : Expression {
    std::unique_ptr<VariableExpression> lvalue;
    IncrementExpression(std::unique_ptr<VariableExpression> lv, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), lvalue(std::move(lv)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct DecrementExpression : Expression {
    std::unique_ptr<VariableExpression> lvalue;
    DecrementExpression(std::unique_ptr<VariableExpression> lv, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), lvalue(std::move(lv)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct AwaitExpression : Expression {
    std::unique_ptr<Expression> expr;
    AwaitExpression(std::unique_ptr<Expression> e, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c), expr(std::move(e)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
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
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct VariableDeclaration : Statement {
    DataType type;
    std::string name;
    int name_line;
    int name_col;
    std::unique_ptr<Expression> init;
    VariableDeclaration(DataType t, std::string n, int nl, int nc, std::unique_ptr<Expression> i, std::string fn, int l,
                        int c)
        : Statement(std::move(fn), l, c),
          type(t),
          name(std::move(n)),
          name_line(nl),
          name_col(nc),
          init(std::move(i)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct Parameter {
    DataType type;
    std::string name;
};

struct Function : Expression {
    DataType return_type;
    std::string name;
    int name_line;
    int name_col;
    std::vector<Parameter> params;
    bool is_variadic;
    std::unique_ptr<Block> body;
    Function(DataType rt, std::string n, int nl, int nc, std::vector<Parameter> p, bool variadic,
             std::unique_ptr<Block> b, std::string fn, int l, int c)
        : Expression(std::move(fn), l, c),
          return_type(rt),
          name(std::move(n)),
          name_line(nl),
          name_col(nc),
          params(std::move(p)),
          is_variadic(variadic),
          body(std::move(b)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct Include : ASTNode {
    std::string path;
    Include(std::string p, std::string fn, int l, int c) : ASTNode(std::move(fn), l, c), path(std::move(p)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct Program : ASTNode {
    std::vector<std::unique_ptr<Include>> includes;
    std::vector<std::unique_ptr<Function>> functions;
    Program() : ASTNode("", 0, 0) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ASTVisitor& visitor) const override { visitor.visit(*this); }
};

}  // namespace ether::parser

#endif  // ETHER_AST_HPP
