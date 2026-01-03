use std::fmt;

#[derive(Debug, Clone)]
pub struct Program {
    pub statements: Vec<Statement>,
}

#[derive(Debug, Clone)]
pub enum Statement {
    Expression(ExpressionStatement),
    Let(LetStatement),
    Function(FunctionStatement),
    Return(ReturnStatement),
    If(IfStatement),
    While(WhileStatement),
    For(ForStatement),
    Import(ImportStatement),
    Struct(StructStatement),
    Enum(EnumStatement),
    Block(BlockStatement),
}

#[derive(Debug, Clone)]
pub struct ExpressionStatement {
    pub expression: Expression,
}

#[derive(Debug, Clone)]
pub struct LetStatement {
    pub name: String,
    pub type_annotation: Option<Type>,
    pub initializer: Option<Expression>,
}

#[derive(Debug, Clone)]
pub struct FunctionStatement {
    pub name: String,
    pub parameters: Vec<Parameter>,
    pub return_type: Option<Type>,
    pub body: BlockStatement,
}

#[derive(Debug, Clone)]
pub struct Parameter {
    pub name: String,
    pub type_annotation: Type,
}

#[derive(Debug, Clone)]
pub struct ReturnStatement {
    pub value: Option<Expression>,
}

#[derive(Debug, Clone)]
pub struct IfStatement {
    pub condition: Expression,
    pub then_branch: Box<Statement>,
    pub else_branch: Option<Box<Statement>>,
}

#[derive(Debug, Clone)]
pub struct WhileStatement {
    pub condition: Expression,
    pub body: Box<Statement>,
}

#[derive(Debug, Clone)]
pub struct ForStatement {
    pub initializer: Option<Box<Statement>>,
    pub condition: Option<Expression>,
    pub increment: Option<Expression>,
    pub body: Box<Statement>,
}

#[derive(Debug, Clone)]
pub struct ImportStatement {
    pub module_path: Vec<String>,
}

#[derive(Debug, Clone)]
pub struct StructStatement {
    pub name: String,
    pub fields: Vec<StructField>,
}

#[derive(Debug, Clone)]
pub struct StructField {
    pub name: String,
    pub type_annotation: Type,
}

#[derive(Debug, Clone)]
pub struct EnumStatement {
    pub name: String,
    pub variants: Vec<EnumVariant>,
}

#[derive(Debug, Clone)]
pub struct EnumVariant {
    pub name: String,
    pub fields: Option<Vec<Type>>,
}

#[derive(Debug, Clone)]
pub struct BlockStatement {
    pub statements: Vec<Statement>,
}

#[derive(Debug, Clone)]
pub enum Expression {
    Literal(Literal),
    Identifier(String),
    Binary(BinaryExpression),
    Unary(UnaryExpression),
    Call(CallExpression),
    Member(MemberExpression),
    Index(IndexExpression),
    Assignment(AssignmentExpression),
    If(IfExpression),
    Match(MatchExpression),
    Array(ArrayExpression),
    Map(MapExpression),
    Struct(StructExpression),
}

#[derive(Debug, Clone)]
pub enum Literal {
    Integer(i64),
    Float(f64),
    String(String),
    Char(char),
    Boolean(bool),
    Array(Vec<Expression>),
    Map(Vec<(Expression, Expression)>),
}

#[derive(Debug, Clone)]
pub struct BinaryExpression {
    pub left: Box<Expression>,
    pub operator: BinaryOperator,
    pub right: Box<Expression>,
}

#[derive(Debug, Clone)]
pub enum BinaryOperator {
    Plus,
    Minus,
    Multiply,
    Divide,
    Modulo,
    Equal,
    NotEqual,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,
    LogicalAnd,
    LogicalOr,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    LeftShift,
    RightShift,
}

#[derive(Debug, Clone)]
pub struct UnaryExpression {
    pub operator: UnaryOperator,
    pub operand: Box<Expression>,
}

#[derive(Debug, Clone)]
pub enum UnaryOperator {
    Plus,
    Minus,
    LogicalNot,
    BitwiseNot,
}

#[derive(Debug, Clone)]
pub struct CallExpression {
    pub callee: Box<Expression>,
    pub arguments: Vec<Expression>,
}

#[derive(Debug, Clone)]
pub struct MemberExpression {
    pub object: Box<Expression>,
    pub property: String,
}

#[derive(Debug, Clone)]
pub struct IndexExpression {
    pub array: Box<Expression>,
    pub index: Box<Expression>,
}

#[derive(Debug, Clone)]
pub struct AssignmentExpression {
    pub target: Box<Expression>,
    pub operator: AssignmentOperator,
    pub value: Box<Expression>,
}

#[derive(Debug, Clone)]
pub enum AssignmentOperator {
    Assign,
    PlusAssign,
    MinusAssign,
    MultiplyAssign,
    DivideAssign,
}

#[derive(Debug, Clone)]
pub struct IfExpression {
    pub condition: Box<Expression>,
    pub then_branch: Box<Expression>,
    pub else_branch: Box<Expression>,
}

#[derive(Debug, Clone)]
pub struct MatchExpression {
    pub value: Box<Expression>,
    pub arms: Vec<MatchArm>,
}

#[derive(Debug, Clone)]
pub struct MatchArm {
    pub pattern: Pattern,
    pub expression: Expression,
}

#[derive(Debug, Clone)]
pub enum Pattern {
    Literal(Literal),
    Identifier(String),
    Wildcard,
    Tuple(Vec<Pattern>),
    Struct(String, Vec<(String, Pattern)>),
}

#[derive(Debug, Clone)]
pub struct ArrayExpression {
    pub elements: Vec<Expression>,
}

#[derive(Debug, Clone)]
pub struct MapExpression {
    pub entries: Vec<(Expression, Expression)>,
}

#[derive(Debug, Clone)]
pub struct StructExpression {
    pub name: String,
    pub fields: Vec<(String, Expression)>,
}

#[derive(Debug, Clone)]
pub enum Type {
    Primitive(PrimitiveType),
    Array(Box<Type>),
    Map(Box<Type>, Box<Type>),
    Function(Vec<Type>, Box<Type>),
    UserDefined(String),
    Generic(String, Vec<Type>),
}

#[derive(Debug, Clone)]
pub enum PrimitiveType {
    Int,
    Float,
    Bool,
    String,
    Char,
    Any,
}

impl fmt::Display for Type {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Type::Primitive(prim) => write!(f, "{}", prim),
            Type::Array(elem_type) => write!(f, "array<{}>", elem_type),
            Type::Map(key_type, value_type) => write!(f, "map<{}, {}>", key_type, value_type),
            Type::Function(params, return_type) => {
                write!(f, "fn(")?;
                for (i, param) in params.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{}", param)?;
                }
                write!(f, ") -> {}", return_type)
            }
            Type::UserDefined(name) => write!(f, "{}", name),
            Type::Generic(name, args) => {
                write!(f, "{}<", name)?;
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 {
                        write!(f, ", ")?;
                    }
                    write!(f, "{}", arg)?;
                }
                write!(f, ">")
            }
        }
    }
}

impl fmt::Display for PrimitiveType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PrimitiveType::Int => write!(f, "int"),
            PrimitiveType::Float => write!(f, "float"),
            PrimitiveType::Bool => write!(f, "bool"),
            PrimitiveType::String => write!(f, "string"),
            PrimitiveType::Char => write!(f, "char"),
            PrimitiveType::Any => write!(f, "any"),
        }
    }
}

impl fmt::Display for BinaryOperator {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            BinaryOperator::Plus => write!(f, "+"),
            BinaryOperator::Minus => write!(f, "-"),
            BinaryOperator::Multiply => write!(f, "*"),
            BinaryOperator::Divide => write!(f, "/"),
            BinaryOperator::Modulo => write!(f, "%"),
            BinaryOperator::Equal => write!(f, "=="),
            BinaryOperator::NotEqual => write!(f, "!="),
            BinaryOperator::LessThan => write!(f, "<"),
            BinaryOperator::LessEqual => write!(f, "<="),
            BinaryOperator::GreaterThan => write!(f, ">"),
            BinaryOperator::GreaterEqual => write!(f, ">="),
            BinaryOperator::LogicalAnd => write!(f, "&&"),
            BinaryOperator::LogicalOr => write!(f, "||"),
            BinaryOperator::BitwiseAnd => write!(f, "&"),
            BinaryOperator::BitwiseOr => write!(f, "|"),
            BinaryOperator::BitwiseXor => write!(f, "^"),
            BinaryOperator::LeftShift => write!(f, "<<"),
            BinaryOperator::RightShift => write!(f, ">>"),
        }
    }
}

impl fmt::Display for UnaryOperator {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            UnaryOperator::Plus => write!(f, "+"),
            UnaryOperator::Minus => write!(f, "-"),
            UnaryOperator::LogicalNot => write!(f, "!"),
            UnaryOperator::BitwiseNot => write!(f, "~"),
        }
    }
} 