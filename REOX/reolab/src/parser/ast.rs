// REOX Compiler - AST Node Definitions
// Abstract Syntax Tree for REOX language

use crate::lexer::Span;

/// A complete program (list of declarations)
#[derive(Debug, Clone)]
pub struct Program {
    pub declarations: Vec<Decl>,
}

/// Top-level declarations
#[derive(Debug, Clone)]
pub enum Decl {
    Function(FnDecl),
    Struct(StructDecl),
    Import(ImportDecl),
    Extern(ExternDecl),
}

/// Function declaration
#[derive(Debug, Clone)]
pub struct FnDecl {
    pub name: String,
    pub params: Vec<Param>,
    pub return_type: Option<Type>,
    pub body: Block,
    pub span: Span,
}

/// Function parameter
#[derive(Debug, Clone)]
pub struct Param {
    pub name: String,
    pub ty: Type,
    pub span: Span,
}

/// Struct declaration
#[derive(Debug, Clone)]
pub struct StructDecl {
    pub name: String,
    pub fields: Vec<Field>,
    pub span: Span,
}

/// Struct field
#[derive(Debug, Clone)]
pub struct Field {
    pub name: String,
    pub ty: Type,
    pub span: Span,
}

/// Import declaration
#[derive(Debug, Clone)]
pub struct ImportDecl {
    pub path: Vec<String>,
    pub span: Span,
}

/// Extern function declaration
#[derive(Debug, Clone)]
pub struct ExternDecl {
    pub name: String,
    pub params: Vec<Param>,
    pub return_type: Option<Type>,
    pub span: Span,
}

/// Type annotation
#[derive(Debug, Clone, PartialEq)]
pub enum Type {
    Int,
    Float,
    String,
    Bool,
    Void,
    Named(String),
    Array(Box<Type>),
}

/// A block of statements
#[derive(Debug, Clone)]
pub struct Block {
    pub statements: Vec<Stmt>,
    pub span: Span,
}

/// Statements
#[derive(Debug, Clone)]
pub enum Stmt {
    Let(LetStmt),
    Expr(Expr),
    Return(ReturnStmt),
    If(IfStmt),
    While(WhileStmt),
    For(ForStmt),
    Block(Block),
    Break(Span),
    Continue(Span),
}

/// Let statement (variable declaration)
#[derive(Debug, Clone)]
pub struct LetStmt {
    pub name: String,
    pub mutable: bool,
    pub ty: Option<Type>,
    pub init: Option<Expr>,
    pub span: Span,
}

/// Return statement
#[derive(Debug, Clone)]
pub struct ReturnStmt {
    pub value: Option<Expr>,
    pub span: Span,
}

/// If statement
#[derive(Debug, Clone)]
pub struct IfStmt {
    pub condition: Expr,
    pub then_block: Block,
    pub else_block: Option<Block>,
    pub span: Span,
}

/// While loop
#[derive(Debug, Clone)]
pub struct WhileStmt {
    pub condition: Expr,
    pub body: Block,
    pub span: Span,
}

/// For loop
#[derive(Debug, Clone)]
pub struct ForStmt {
    pub var: String,
    pub iterable: Expr,
    pub body: Block,
    pub span: Span,
}

/// Expressions
#[derive(Debug, Clone)]
pub enum Expr {
    Literal(Literal),
    Identifier(String, Span),
    Binary(Box<Expr>, BinOp, Box<Expr>, Span),
    Unary(UnaryOp, Box<Expr>, Span),
    Call(Box<Expr>, Vec<Expr>, Span),
    Member(Box<Expr>, String, Span),
    Index(Box<Expr>, Box<Expr>, Span),
    Assign(Box<Expr>, Box<Expr>, Span),
    StructLit(String, Vec<(String, Expr)>, Span),
    ArrayLit(Vec<Expr>, Span),
    Match(Box<Expr>, Vec<MatchArm>, Span),
}

/// Literal values
#[derive(Debug, Clone)]
pub enum Literal {
    Int(i64, Span),
    Float(f64, Span),
    String(String, Span),
    Bool(bool, Span),
}

/// Binary operators
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BinOp {
    // Arithmetic
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    // Comparison
    Eq,
    Ne,
    Lt,
    Gt,
    Le,
    Ge,
    // Logical
    And,
    Or,
}

impl BinOp {
    /// Get operator precedence (higher = binds tighter)
    pub fn precedence(&self) -> u8 {
        match self {
            BinOp::Or => 1,
            BinOp::And => 2,
            BinOp::Eq | BinOp::Ne => 3,
            BinOp::Lt | BinOp::Gt | BinOp::Le | BinOp::Ge => 4,
            BinOp::Add | BinOp::Sub => 5,
            BinOp::Mul | BinOp::Div | BinOp::Mod => 6,
        }
    }
}

/// Unary operators
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UnaryOp {
    Neg,
    Not,
}

/// Match arm
#[derive(Debug, Clone)]
pub struct MatchArm {
    pub pattern: Pattern,
    pub body: Expr,
    pub span: Span,
}

/// Match patterns
#[derive(Debug, Clone)]
pub enum Pattern {
    Literal(Literal),
    Identifier(String),
    Wildcard,
}
