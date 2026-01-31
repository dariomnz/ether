#ifndef ETHER_AST_HPP
#define ETHER_AST_HPP

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ether::parser {
struct IntegerLiteral;
struct FloatLiteral;
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
struct StructDeclaration;
struct MemberAccessExpression;
struct IndexExpression;
struct SizeofExpression;
struct Program;

struct ConstASTVisitor {
    virtual ~ConstASTVisitor() = default;
    virtual void visit(const IntegerLiteral& node) = 0;
    virtual void visit(const FloatLiteral& node) = 0;
    virtual void visit(const StringLiteral& node) = 0;
    virtual void visit(const VariableExpression& node) = 0;
    virtual void visit(const FunctionCall& node) = 0;
    virtual void visit(const VarargExpression& node) = 0;
    virtual void visit(const BinaryExpression& node) = 0;
    virtual void visit(const Block& node) = 0;
    virtual void visit(const IfStatement& node) = 0;
    virtual void visit(const ReturnStatement& node) = 0;
    virtual void visit(const ExpressionStatement& node) = 0;
    virtual void visit(const YieldStatement& node) = 0;
    virtual void visit(const SpawnExpression& node) = 0;
    virtual void visit(const AssignmentExpression& node) = 0;
    virtual void visit(const IncrementExpression& node) = 0;
    virtual void visit(const DecrementExpression& node) = 0;
    virtual void visit(const AwaitExpression& node) = 0;
    virtual void visit(const ForStatement& node) = 0;
    virtual void visit(const VariableDeclaration& node) = 0;
    virtual void visit(const Function& node) = 0;
    virtual void visit(const Include& node) = 0;
    virtual void visit(const StructDeclaration& node) = 0;
    virtual void visit(const MemberAccessExpression& node) = 0;
    virtual void visit(const IndexExpression& node) = 0;
    virtual void visit(const SizeofExpression& node) = 0;
    virtual void visit(const Program& node) = 0;
};

struct ASTVisitor {
    virtual ~ASTVisitor() = default;
    virtual void visit(IntegerLiteral& node) = 0;
    virtual void visit(FloatLiteral& node) = 0;
    virtual void visit(StringLiteral& node) = 0;
    virtual void visit(VariableExpression& node) = 0;
    virtual void visit(FunctionCall& node) = 0;
    virtual void visit(VarargExpression& node) = 0;
    virtual void visit(BinaryExpression& node) = 0;
    virtual void visit(Block& node) = 0;
    virtual void visit(IfStatement& node) = 0;
    virtual void visit(ReturnStatement& node) = 0;
    virtual void visit(ExpressionStatement& node) = 0;
    virtual void visit(YieldStatement& node) = 0;
    virtual void visit(SpawnExpression& node) = 0;
    virtual void visit(AssignmentExpression& node) = 0;
    virtual void visit(IncrementExpression& node) = 0;
    virtual void visit(DecrementExpression& node) = 0;
    virtual void visit(AwaitExpression& node) = 0;
    virtual void visit(ForStatement& node) = 0;
    virtual void visit(VariableDeclaration& node) = 0;
    virtual void visit(Function& node) = 0;
    virtual void visit(Include& node) = 0;
    virtual void visit(StructDeclaration& node) = 0;
    virtual void visit(MemberAccessExpression& node) = 0;
    virtual void visit(IndexExpression& node) = 0;
    virtual void visit(SizeofExpression& node) = 0;
    virtual void visit(Program& node) = 0;
};

struct DefaultIgnoreConstASTVisitor : public ConstASTVisitor {
    virtual ~DefaultIgnoreConstASTVisitor() = default;
    virtual void visit(const IntegerLiteral& node) {};
    virtual void visit(const FloatLiteral& node) {};
    virtual void visit(const StringLiteral& node) {};
    virtual void visit(const VariableExpression& node) {};
    virtual void visit(const FunctionCall& node) {};
    virtual void visit(const VarargExpression& node) {};
    virtual void visit(const BinaryExpression& node) {};
    virtual void visit(const Block& node) {};
    virtual void visit(const IfStatement& node) {};
    virtual void visit(const ReturnStatement& node) {};
    virtual void visit(const ExpressionStatement& node) {};
    virtual void visit(const YieldStatement& node) {};
    virtual void visit(const SpawnExpression& node) {};
    virtual void visit(const AssignmentExpression& node) {};
    virtual void visit(const IncrementExpression& node) {};
    virtual void visit(const DecrementExpression& node) {};
    virtual void visit(const AwaitExpression& node) {};
    virtual void visit(const ForStatement& node) {};
    virtual void visit(const VariableDeclaration& node) {};
    virtual void visit(const Function& node) {};
    virtual void visit(const Include& node) {};
    virtual void visit(const StructDeclaration& node) {};
    virtual void visit(const MemberAccessExpression& node) {};
    virtual void visit(const IndexExpression& node) {};
    virtual void visit(const SizeofExpression& node) {};
    virtual void visit(const Program& node) {};
};

struct ASTNode {
    std::string filename;
    int line;
    int column;
    int length;
    ASTNode(std::string fn, int l, int c, int len = 1) : filename(std::move(fn)), line(l), column(c), length(len) {}
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;
    virtual void accept(ConstASTVisitor& visitor) const = 0;
};

struct DataType {
    enum class Kind { I64, I32, I16, I8, F64, F32, Coroutine, Void, Ptr, String, Struct, Array };
    Kind kind;
    std::string struct_name;  // Only for Kind::Struct
    std::shared_ptr<DataType> inner;
    uint32_t array_size = 0;  // Only for Kind::Array

    DataType() : kind(Kind::I32), inner(nullptr) {}
    explicit DataType(Kind k, std::shared_ptr<DataType> i = nullptr) : kind(k), inner(i) {}
    DataType(Kind k, std::string name) : kind(k), struct_name(std::move(name)), inner(nullptr) {}
    DataType(Kind k, std::shared_ptr<DataType> i, uint32_t size) : kind(k), inner(i), array_size(size) {}

    bool operator==(const DataType& other) const {
        if (kind != other.kind) return false;
        if (kind == Kind::Struct) return struct_name == other.struct_name;
        if (kind == Kind::Array) {
            if (!inner || !other.inner) return false;
            return *inner == *other.inner && array_size == other.array_size;
        }
        if (inner && other.inner) return *inner == *other.inner;
        return !inner && !other.inner;
    }

    bool is_integer() const { return kind == Kind::I64 || kind == Kind::I32 || kind == Kind::I16 || kind == Kind::I8; }
    bool is_float() const { return kind == Kind::F64 || kind == Kind::F32; }

    friend std::ostream& operator<<(std::ostream& os, const DataType& type) {
        static const std::unordered_map<Kind, std::string_view> kind_to_str = {
            {Kind::I64, "i64"},
            {Kind::I32, "i32"},
            {Kind::I16, "i16"},
            {Kind::I8, "i8"},
            {Kind::F64, "f64"},
            {Kind::F32, "f32"},
            {Kind::Coroutine, "coroutine"},
            {Kind::Void, "void"},
            {Kind::Ptr, "ptr"},
            {Kind::String, "string"},
            {Kind::Struct, "struct"},
            {Kind::Array, "array "},
        };
        auto it = kind_to_str.find(type.kind);
        if (it != kind_to_str.end()) {
            os << it->second;
        } else {
            os << "UNKNOWN";
        }
        if (type.kind == Kind::Array) {
            if (type.inner) {
                os << *type.inner;
            }
            os << "[" << type.array_size << "]";
        } else if (type.inner) {
            os << "(" << *type.inner << ")";
        }
        return os;
    }

    std::string to_string() const {
        std::stringstream ss;
        ss << *this;
        return ss.str();
    }
};

struct Expression : ASTNode {
    std::unique_ptr<DataType> type;  // Assigned during semantic analysis
    Expression(std::string fn, int l, int c, int len = 1) : ASTNode(std::move(fn), l, c, len) {}
};
struct Statement : ASTNode {
    Statement(std::string fn, int l, int c, int len = 1) : ASTNode(std::move(fn), l, c, len) {}
};

struct IntegerLiteral : Expression {
    int64_t value;
    IntegerLiteral(int64_t val, std::string fn, int l, int c, int len)
        : Expression(std::move(fn), l, c, len), value(val) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct FloatLiteral : Expression {
    double value;
    bool is_f32;
    FloatLiteral(double val, bool f32, std::string fn, int l, int c, int len)
        : Expression(std::move(fn), l, c, len), value(val), is_f32(f32) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct StringLiteral : Expression {
    std::string value;
    StringLiteral(std::string v, std::string fn, int l, int c, int len)
        : Expression(std::move(fn), l, c, len), value(std::move(v)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct VariableExpression : Expression {
    std::string name;
    std::string decl_filename;
    int decl_line = 0;
    int decl_col = 0;
    VariableExpression(std::string n, std::string fn, int l, int c, int len)
        : Expression(std::move(fn), l, c, len), name(std::move(n)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct FunctionCall : Expression {
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    std::string decl_filename;
    int decl_line = 0;
    int decl_col = 0;
    std::vector<DataType> param_types;
    bool is_variadic = false;
    std::unique_ptr<Expression> object = nullptr;  // For method calls
    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a, std::string fn, int l, int c, int len,
                 std::unique_ptr<Expression> obj = nullptr)
        : Expression(std::move(fn), l, c, len), name(std::move(n)), args(std::move(a)), object(std::move(obj)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct VarargExpression : Expression {
    VarargExpression(std::string fn, int l, int c, int len) : Expression(std::move(fn), l, c, len) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct BinaryExpression : Expression {
    enum class Op { Add, Sub, Mul, Div, Eq, Leq, Less, Gt, Geq };
    Op op;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;

    BinaryExpression(Op o, std::unique_ptr<Expression> l, std::unique_ptr<Expression> r, std::string fn, int line,
                     int c, int len)
        : Expression(std::move(fn), line, c, len), op(o), left(std::move(l)), right(std::move(r)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct Block : Statement {
    std::vector<std::unique_ptr<Statement>> statements;
    Block(std::string fn, int l, int c, int len = 1) : Statement(std::move(fn), l, c, len) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct IfStatement : Statement {
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> then_branch;
    std::unique_ptr<Block> else_branch;  // optional
    IfStatement(std::unique_ptr<Expression> cond, std::unique_ptr<Block> tb, std::unique_ptr<Block> eb, std::string fn,
                int l, int c, int len)
        : Statement(std::move(fn), l, c, len),
          condition(std::move(cond)),
          then_branch(std::move(tb)),
          else_branch(std::move(eb)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ReturnStatement : Statement {
    std::unique_ptr<Expression> expr;
    ReturnStatement(std::unique_ptr<Expression> e, std::string fn, int l, int c, int len)
        : Statement(std::move(fn), l, c, len), expr(std::move(e)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ExpressionStatement : Statement {
    std::unique_ptr<Expression> expr;
    ExpressionStatement(std::unique_ptr<Expression> e, std::string fn, int l, int c, int len)
        : Statement(std::move(fn), l, c, len), expr(std::move(e)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct YieldStatement : Statement {
    YieldStatement(std::string fn, int l, int c, int len) : Statement(std::move(fn), l, c, len) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct SpawnExpression : Expression {
    std::unique_ptr<FunctionCall> call;
    SpawnExpression(std::unique_ptr<FunctionCall> c, std::string fn, int l, int c_pos, int len)
        : Expression(std::move(fn), l, c_pos, len), call(std::move(c)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct AssignmentExpression : Expression {
    std::unique_ptr<Expression> lvalue;
    std::unique_ptr<Expression> value;
    AssignmentExpression(std::unique_ptr<Expression> lv, std::unique_ptr<Expression> v, std::string fn, int l, int c,
                         int len)
        : Expression(std::move(fn), l, c, len), lvalue(std::move(lv)), value(std::move(v)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct IncrementExpression : Expression {
    std::unique_ptr<Expression> lvalue;
    IncrementExpression(std::unique_ptr<Expression> lv, std::string fn, int l, int c, int len)
        : Expression(std::move(fn), l, c, len), lvalue(std::move(lv)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct DecrementExpression : Expression {
    std::unique_ptr<Expression> lvalue;
    DecrementExpression(std::unique_ptr<Expression> lv, std::string fn, int l, int c, int len)
        : Expression(std::move(fn), l, c, len), lvalue(std::move(lv)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct AwaitExpression : Expression {
    std::unique_ptr<Expression> expr;
    AwaitExpression(std::unique_ptr<Expression> e, std::string fn, int l, int c, int len)
        : Expression(std::move(fn), l, c, len), expr(std::move(e)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct SizeofExpression : Expression {
    DataType target_type;
    uint32_t calculated_size = 0;
    int type_line;
    int type_col;
    SizeofExpression(DataType t, int tl, int tc, std::string fn, int l, int c, int len)
        : Expression(std::move(fn), l, c, len), target_type(t), type_line(tl), type_col(tc) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct MemberAccessExpression : Expression {
    std::unique_ptr<Expression> object;
    std::string member_name;
    MemberAccessExpression(std::unique_ptr<Expression> obj, std::string mem, std::string fn, int l, int c, int len)
        : Expression(std::move(fn), l, c, len), object(std::move(obj)), member_name(std::move(mem)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct IndexExpression : Expression {
    std::unique_ptr<Expression> object;
    std::unique_ptr<Expression> index;
    IndexExpression(std::unique_ptr<Expression> obj, std::unique_ptr<Expression> idx, std::string fn, int l, int c,
                    int len)
        : Expression(std::move(fn), l, c, len), object(std::move(obj)), index(std::move(idx)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct ForStatement : Statement {
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    std::unique_ptr<Block> body;
    ForStatement(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc,
                 std::unique_ptr<Block> b, std::string fn, int l, int c_pos, int len)
        : Statement(std::move(fn), l, c_pos, len),
          init(std::move(i)),
          condition(std::move(c)),
          increment(std::move(inc)),
          body(std::move(b)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct VariableDeclaration : Statement {
    DataType type;
    std::string name;
    int name_line;
    int name_col;
    std::unique_ptr<Expression> init;
    VariableDeclaration(DataType t, std::string n, int nl, int nc, std::unique_ptr<Expression> i, std::string fn, int l,
                        int c, int len)
        : Statement(std::move(fn), l, c, len),
          type(t),
          name(std::move(n)),
          name_line(nl),
          name_col(nc),
          init(std::move(i)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct Parameter {
    DataType type;
    std::string name;
    int line;
    int col;
    int name_line;
    int name_col;
};

struct Function : Expression {
    DataType return_type;
    std::string name;
    int name_line;
    int name_col;
    std::vector<Parameter> params;
    bool is_variadic;
    std::unique_ptr<Block> body;
    std::string struct_name;  // For methods
    Function(DataType rt, std::string n, int nl, int nc, std::vector<Parameter> p, bool variadic,
             std::unique_ptr<Block> b, std::string fn, int l, int c, int len, std::string sn = "")
        : Expression(std::move(fn), l, c, len),
          return_type(rt),
          name(std::move(n)),
          name_line(nl),
          name_col(nc),
          params(std::move(p)),
          is_variadic(variadic),
          body(std::move(b)),
          struct_name(std::move(sn)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct Include : ASTNode {
    std::string path;
    Include(std::string p, std::string fn, int l, int c, int len)
        : ASTNode(std::move(fn), l, c, len), path(std::move(p)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct StructDeclaration : ASTNode {
    std::string name;
    int name_line;
    int name_col;
    std::vector<Parameter> members;
    StructDeclaration(std::string n, int nl, int nc, std::vector<Parameter> m, std::string fn, int l, int c, int len)
        : ASTNode(std::move(fn), l, c, len), name(std::move(n)), name_line(nl), name_col(nc), members(std::move(m)) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

struct Program : ASTNode {
    std::vector<std::unique_ptr<Include>> includes;
    std::vector<std::unique_ptr<StructDeclaration>> structs;
    std::vector<std::unique_ptr<VariableDeclaration>> globals;
    std::vector<std::unique_ptr<Function>> functions;
    Program() : ASTNode("", 0, 0) {}
    void accept(ASTVisitor& visitor) override { visitor.visit(*this); }
    void accept(ConstASTVisitor& visitor) const override { visitor.visit(*this); }
};

}  // namespace ether::parser

#endif  // ETHER_AST_HPP
