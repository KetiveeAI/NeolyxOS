// REOX Compiler - Type Checker
// Validates types and builds symbol table
// Zero external dependencies

mod types;

pub use types::*;

use crate::parser::{
    Ast, Decl, Stmt, Expr, Literal, BinOp, UnaryOp,
    FnDecl, StructDecl, ExternDecl, Block, Type, LetStmt,
    ReturnStmt, IfStmt, WhileStmt, ForStmt,
};
use crate::lexer::Span;

/// Type check error
#[derive(Debug, Clone)]
pub struct TypeError {
    pub message: String,
    pub line: u32,
    pub column: u32,
}

impl TypeError {
    pub fn new(message: impl Into<String>, span: &Span) -> Self {
        Self {
            message: message.into(),
            line: span.line,
            column: span.column,
        }
    }

    pub fn display(&self) -> String {
        format!("type error[{}:{}]: {}", self.line, self.column, self.message)
    }
}

/// Type checker state
pub struct TypeChecker {
    symbols: SymbolTable,
    errors: Vec<TypeError>,
    current_return_type: Option<ResolvedType>,
}

impl TypeChecker {
    pub fn new() -> Self {
        Self {
            symbols: SymbolTable::new(),
            errors: Vec::new(),
            current_return_type: None,
        }
    }

    /// Type check the entire AST
    pub fn check_program(&mut self, ast: &Ast) -> Result<(), Vec<TypeError>> {
        // First pass: collect all struct and function declarations
        for decl in &ast.declarations {
            match decl {
                Decl::Struct(s) => self.register_struct(s),
                Decl::Function(f) => self.register_function(f),
                Decl::Extern(e) => self.register_extern(e),
                Decl::Import(_) => {} // Skip imports for now
            }
        }

        // Second pass: type check function bodies
        for decl in &ast.declarations {
            if let Decl::Function(f) = decl {
                self.check_function(f);
            }
        }

        if self.errors.is_empty() {
            Ok(())
        } else {
            Err(self.errors.clone())
        }
    }

    fn register_struct(&mut self, s: &StructDecl) {
        let mut fields = std::collections::HashMap::new();
        for field in &s.fields {
            let ty = ResolvedType::from_parser_type(&field.ty);
            fields.insert(field.name.clone(), ty);
        }

        if let Err(e) = self.symbols.define_struct(StructInfo {
            name: s.name.clone(),
            fields,
        }) {
            self.errors.push(TypeError {
                message: e,
                line: s.span.line,
                column: s.span.column,
            });
        }
    }

    fn register_function(&mut self, f: &FnDecl) {
        let params: Vec<ResolvedType> = f.params
            .iter()
            .map(|p| ResolvedType::from_parser_type(&p.ty))
            .collect();

        let ret = f.return_type
            .as_ref()
            .map(|t| ResolvedType::from_parser_type(t))
            .unwrap_or(ResolvedType::Void);

        let fn_type = ResolvedType::Function {
            params,
            ret: Box::new(ret),
        };

        if let Err(e) = self.symbols.define_function(f.name.clone(), fn_type.clone()) {
            self.errors.push(TypeError {
                message: e,
                line: f.span.line,
                column: f.span.column,
            });
        }

        // Also add to symbol table for lookup
        let _ = self.symbols.define(Symbol {
            name: f.name.clone(),
            ty: fn_type,
            mutable: false,
            kind: SymbolKind::Function,
        });
    }

    fn register_extern(&mut self, e: &ExternDecl) {
        let params: Vec<ResolvedType> = e.params
            .iter()
            .map(|p| ResolvedType::from_parser_type(&p.ty))
            .collect();

        let ret = e.return_type
            .as_ref()
            .map(|t| ResolvedType::from_parser_type(t))
            .unwrap_or(ResolvedType::Void);

        let fn_type = ResolvedType::Function {
            params,
            ret: Box::new(ret),
        };

        if let Err(e_msg) = self.symbols.define_function(e.name.clone(), fn_type.clone()) {
            self.errors.push(TypeError {
                message: e_msg,
                line: e.span.line,
                column: e.span.column,
            });
        }

        let _ = self.symbols.define(Symbol {
            name: e.name.clone(),
            ty: fn_type,
            mutable: false,
            kind: SymbolKind::Function,
        });
    }

    fn check_function(&mut self, f: &FnDecl) {
        self.symbols.push_scope();

        // Add parameters to scope
        for param in &f.params {
            let ty = ResolvedType::from_parser_type(&param.ty);
            let _ = self.symbols.define(Symbol {
                name: param.name.clone(),
                ty,
                mutable: false,
                kind: SymbolKind::Parameter,
            });
        }

        // Set expected return type
        self.current_return_type = f.return_type
            .as_ref()
            .map(|t| ResolvedType::from_parser_type(t));

        // Check function body
        self.check_block(&f.body);

        self.current_return_type = None;
        self.symbols.pop_scope();
    }

    fn check_block(&mut self, block: &Block) {
        for stmt in &block.statements {
            self.check_statement(stmt);
        }
    }

    fn check_statement(&mut self, stmt: &Stmt) {
        match stmt {
            Stmt::Let(l) => self.check_let(l),
            Stmt::Return(r) => self.check_return(r),
            Stmt::If(i) => self.check_if(i),
            Stmt::While(w) => self.check_while(w),
            Stmt::For(f) => self.check_for(f),
            Stmt::Expr(e) => { self.infer_expr_type(e); }
            Stmt::Block(b) => {
                self.symbols.push_scope();
                self.check_block(b);
                self.symbols.pop_scope();
            }
            Stmt::Break(_) | Stmt::Continue(_) => {}
        }
    }

    fn check_let(&mut self, l: &LetStmt) {
        let declared_type = l.ty.as_ref().map(|t| ResolvedType::from_parser_type(t));
        
        let inferred_type = l.init.as_ref().map(|e| self.infer_expr_type(e));

        let final_type = match (&declared_type, &inferred_type) {
            (Some(decl), Some(infer)) => {
                if !decl.is_assignable_from(infer) {
                    self.errors.push(TypeError {
                        message: format!(
                            "type mismatch: expected '{}', found '{}'",
                            decl.display_name(),
                            infer.display_name()
                        ),
                        line: l.span.line,
                        column: l.span.column,
                    });
                }
                decl.clone()
            }
            (Some(decl), None) => decl.clone(),
            (None, Some(infer)) => infer.clone(),
            (None, None) => {
                self.errors.push(TypeError {
                    message: "cannot infer type without initializer".to_string(),
                    line: l.span.line,
                    column: l.span.column,
                });
                ResolvedType::Error
            }
        };

        if let Err(e) = self.symbols.define(Symbol {
            name: l.name.clone(),
            ty: final_type,
            mutable: l.mutable,
            kind: SymbolKind::Variable,
        }) {
            self.errors.push(TypeError {
                message: e,
                line: l.span.line,
                column: l.span.column,
            });
        }
    }

    fn check_return(&mut self, r: &ReturnStmt) {
        let return_type = r.value.as_ref()
            .map(|e| self.infer_expr_type(e))
            .unwrap_or(ResolvedType::Void);

        if let Some(expected) = &self.current_return_type {
            if !expected.is_assignable_from(&return_type) {
                self.errors.push(TypeError {
                    message: format!(
                        "return type mismatch: expected '{}', found '{}'",
                        expected.display_name(),
                        return_type.display_name()
                    ),
                    line: r.span.line,
                    column: r.span.column,
                });
            }
        }
    }

    fn check_if(&mut self, i: &IfStmt) {
        let cond_type = self.infer_expr_type(&i.condition);
        if cond_type != ResolvedType::Bool {
            self.errors.push(TypeError {
                message: format!(
                    "if condition must be bool, found '{}'",
                    cond_type.display_name()
                ),
                line: i.span.line,
                column: i.span.column,
            });
        }

        self.symbols.push_scope();
        self.check_block(&i.then_block);
        self.symbols.pop_scope();

        if let Some(else_block) = &i.else_block {
            self.symbols.push_scope();
            self.check_block(else_block);
            self.symbols.pop_scope();
        }
    }

    fn check_while(&mut self, w: &WhileStmt) {
        let cond_type = self.infer_expr_type(&w.condition);
        if cond_type != ResolvedType::Bool {
            self.errors.push(TypeError {
                message: format!(
                    "while condition must be bool, found '{}'",
                    cond_type.display_name()
                ),
                line: w.span.line,
                column: w.span.column,
            });
        }

        self.symbols.push_scope();
        self.check_block(&w.body);
        self.symbols.pop_scope();
    }

    fn check_for(&mut self, f: &ForStmt) {
        let iter_type = self.infer_expr_type(&f.iterable);
        
        let elem_type = match iter_type {
            ResolvedType::Array(inner) => *inner,
            ResolvedType::Int => ResolvedType::Int, // For range-like iteration
            _ => {
                self.errors.push(TypeError {
                    message: format!(
                        "cannot iterate over '{}'",
                        iter_type.display_name()
                    ),
                    line: f.span.line,
                    column: f.span.column,
                });
                ResolvedType::Error
            }
        };

        self.symbols.push_scope();
        let _ = self.symbols.define(Symbol {
            name: f.var.clone(),
            ty: elem_type,
            mutable: false,
            kind: SymbolKind::Variable,
        });
        self.check_block(&f.body);
        self.symbols.pop_scope();
    }

    /// Infer the type of an expression
    fn infer_expr_type(&mut self, expr: &Expr) -> ResolvedType {
        match expr {
            Expr::Literal(lit) => self.infer_literal_type(lit),
            Expr::Identifier(name, span) => {
                if let Some(sym) = self.symbols.lookup(name) {
                    sym.ty.clone()
                } else {
                    self.errors.push(TypeError::new(
                        format!("undefined variable '{}'", name),
                        span,
                    ));
                    ResolvedType::Error
                }
            }
            Expr::Binary(left, op, right, span) => {
                let left_ty = self.infer_expr_type(left);
                let right_ty = self.infer_expr_type(right);
                self.check_binary_op(&left_ty, op, &right_ty, span)
            }
            Expr::Unary(op, operand, span) => {
                let operand_ty = self.infer_expr_type(operand);
                self.check_unary_op(op, &operand_ty, span)
            }
            Expr::Call(callee, args, span) => {
                let callee_ty = self.infer_expr_type(callee);
                self.check_call(&callee_ty, args, span)
            }
            Expr::Member(obj, field, span) => {
                let obj_ty = self.infer_expr_type(obj);
                self.check_member_access(&obj_ty, field, span)
            }
            Expr::Index(arr, idx, span) => {
                let arr_ty = self.infer_expr_type(arr);
                let idx_ty = self.infer_expr_type(idx);
                self.check_index(&arr_ty, &idx_ty, span)
            }
            Expr::Assign(target, value, span) => {
                let target_ty = self.infer_expr_type(target);
                let value_ty = self.infer_expr_type(value);
                if !target_ty.is_assignable_from(&value_ty) {
                    self.errors.push(TypeError::new(
                        format!(
                            "cannot assign '{}' to '{}'",
                            value_ty.display_name(),
                            target_ty.display_name()
                        ),
                        span,
                    ));
                }
                target_ty
            }
            Expr::StructLit(name, fields, span) => {
                if let Some(struct_info) = self.symbols.lookup_struct(name) {
                    // Clone fields to avoid borrow conflict
                    let expected_fields = struct_info.fields.clone();
                    
                    // Check all fields are provided with correct types
                    for (field_name, value) in fields {
                        let value_ty = self.infer_expr_type(value);
                        if let Some(expected_ty) = expected_fields.get(field_name) {
                            if !expected_ty.is_assignable_from(&value_ty) {
                                self.errors.push(TypeError::new(
                                    format!(
                                        "field '{}' expects '{}', found '{}'",
                                        field_name,
                                        expected_ty.display_name(),
                                        value_ty.display_name()
                                    ),
                                    span,
                                ));
                            }
                        } else {
                            self.errors.push(TypeError::new(
                                format!("struct '{}' has no field '{}'", name, field_name),
                                span,
                            ));
                        }
                    }
                    ResolvedType::Struct(name.clone())
                } else {
                    self.errors.push(TypeError::new(
                        format!("undefined struct '{}'", name),
                        span,
                    ));
                    ResolvedType::Error
                }
            }
            Expr::ArrayLit(elements, _) => {
                if elements.is_empty() {
                    ResolvedType::Array(Box::new(ResolvedType::Unknown))
                } else {
                    let elem_ty = self.infer_expr_type(&elements[0]);
                    ResolvedType::Array(Box::new(elem_ty))
                }
            }
            Expr::Match(_, _, _) => {
                // Match expressions are complex - return Unknown for now
                ResolvedType::Unknown
            }
        }
    }

    fn infer_literal_type(&self, lit: &Literal) -> ResolvedType {
        match lit {
            Literal::Int(_, _) => ResolvedType::Int,
            Literal::Float(_, _) => ResolvedType::Float,
            Literal::String(_, _) => ResolvedType::String,
            Literal::Bool(_, _) => ResolvedType::Bool,
        }
    }

    fn check_binary_op(&mut self, left: &ResolvedType, op: &BinOp, right: &ResolvedType, span: &Span) -> ResolvedType {
        match op {
            // Arithmetic operators
            BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Mod => {
                match (left, right) {
                    (ResolvedType::Int, ResolvedType::Int) => ResolvedType::Int,
                    (ResolvedType::Float, ResolvedType::Float) => ResolvedType::Float,
                    (ResolvedType::Float, ResolvedType::Int) => ResolvedType::Float,
                    (ResolvedType::Int, ResolvedType::Float) => ResolvedType::Float,
                    (ResolvedType::String, ResolvedType::String) if *op == BinOp::Add => {
                        ResolvedType::String // String concatenation
                    }
                    _ => {
                        self.errors.push(TypeError::new(
                            format!(
                                "cannot apply operator to '{}' and '{}'",
                                left.display_name(),
                                right.display_name()
                            ),
                            span,
                        ));
                        ResolvedType::Error
                    }
                }
            }
            // Comparison operators
            BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Gt | BinOp::Le | BinOp::Ge => {
                if left == right || 
                   (matches!(left, ResolvedType::Int | ResolvedType::Float) && 
                    matches!(right, ResolvedType::Int | ResolvedType::Float)) {
                    ResolvedType::Bool
                } else {
                    self.errors.push(TypeError::new(
                        format!(
                            "cannot compare '{}' and '{}'",
                            left.display_name(),
                            right.display_name()
                        ),
                        span,
                    ));
                    ResolvedType::Error
                }
            }
            // Logical operators
            BinOp::And | BinOp::Or => {
                if *left == ResolvedType::Bool && *right == ResolvedType::Bool {
                    ResolvedType::Bool
                } else {
                    self.errors.push(TypeError::new(
                        format!(
                            "logical operators require bool operands, found '{}' and '{}'",
                            left.display_name(),
                            right.display_name()
                        ),
                        span,
                    ));
                    ResolvedType::Error
                }
            }
        }
    }

    fn check_unary_op(&mut self, op: &UnaryOp, operand: &ResolvedType, span: &Span) -> ResolvedType {
        match op {
            UnaryOp::Neg => {
                if matches!(operand, ResolvedType::Int | ResolvedType::Float) {
                    operand.clone()
                } else {
                    self.errors.push(TypeError::new(
                        format!("cannot negate '{}'", operand.display_name()),
                        span,
                    ));
                    ResolvedType::Error
                }
            }
            UnaryOp::Not => {
                if *operand == ResolvedType::Bool {
                    ResolvedType::Bool
                } else {
                    self.errors.push(TypeError::new(
                        format!("cannot apply '!' to '{}'", operand.display_name()),
                        span,
                    ));
                    ResolvedType::Error
                }
            }
        }
    }

    fn check_call(&mut self, callee: &ResolvedType, args: &[Expr], span: &Span) -> ResolvedType {
        match callee {
            ResolvedType::Function { params, ret } => {
                if args.len() != params.len() {
                    self.errors.push(TypeError::new(
                        format!(
                            "expected {} arguments, found {}",
                            params.len(),
                            args.len()
                        ),
                        span,
                    ));
                    return *ret.clone();
                }

                for (i, (arg, param)) in args.iter().zip(params.iter()).enumerate() {
                    let arg_ty = self.infer_expr_type(arg);
                    if !param.is_assignable_from(&arg_ty) {
                        self.errors.push(TypeError::new(
                            format!(
                                "argument {} type mismatch: expected '{}', found '{}'",
                                i + 1,
                                param.display_name(),
                                arg_ty.display_name()
                            ),
                            span,
                        ));
                    }
                }

                *ret.clone()
            }
            ResolvedType::Error => ResolvedType::Error,
            _ => {
                self.errors.push(TypeError::new(
                    format!("'{}' is not callable", callee.display_name()),
                    span,
                ));
                ResolvedType::Error
            }
        }
    }

    fn check_member_access(&mut self, obj: &ResolvedType, field: &str, span: &Span) -> ResolvedType {
        match obj {
            ResolvedType::Struct(name) => {
                if let Some(struct_info) = self.symbols.lookup_struct(name) {
                    if let Some(field_ty) = struct_info.fields.get(field) {
                        field_ty.clone()
                    } else {
                        self.errors.push(TypeError::new(
                            format!("struct '{}' has no field '{}'", name, field),
                            span,
                        ));
                        ResolvedType::Error
                    }
                } else {
                    self.errors.push(TypeError::new(
                        format!("undefined struct '{}'", name),
                        span,
                    ));
                    ResolvedType::Error
                }
            }
            ResolvedType::Error => ResolvedType::Error,
            _ => {
                self.errors.push(TypeError::new(
                    format!("cannot access field '{}' on '{}'", field, obj.display_name()),
                    span,
                ));
                ResolvedType::Error
            }
        }
    }

    fn check_index(&mut self, arr: &ResolvedType, idx: &ResolvedType, span: &Span) -> ResolvedType {
        if *idx != ResolvedType::Int {
            self.errors.push(TypeError::new(
                format!("array index must be int, found '{}'", idx.display_name()),
                span,
            ));
        }

        match arr {
            ResolvedType::Array(inner) => *inner.clone(),
            ResolvedType::String => ResolvedType::String, // String indexing returns char/string
            ResolvedType::Error => ResolvedType::Error,
            _ => {
                self.errors.push(TypeError::new(
                    format!("cannot index into '{}'", arr.display_name()),
                    span,
                ));
                ResolvedType::Error
            }
        }
    }
}

impl Default for TypeChecker {
    fn default() -> Self {
        Self::new()
    }
}

/// Type check the AST (convenience function for backward compatibility)
pub fn check(ast: &Ast) -> Ast {
    let mut checker = TypeChecker::new();
    if let Err(errors) = checker.check_program(ast) {
        for error in errors {
            eprintln!("{}", error.display());
        }
    }
    ast.clone()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::tokenize;
    use crate::parser::parse;

    fn check_source(source: &str) -> Result<(), Vec<TypeError>> {
        let tokens = tokenize(source).unwrap();
        let ast = parse(&tokens);
        let mut checker = TypeChecker::new();
        checker.check_program(&ast)
    }

    #[test]
    fn test_valid_function() {
        let result = check_source(r#"
            fn add(a: int, b: int) -> int {
                return a + b;
            }
        "#);
        assert!(result.is_ok());
    }

    #[test]
    fn test_undefined_variable() {
        let result = check_source(r#"
            fn main() {
                let x: int = y;
            }
        "#);
        assert!(result.is_err());
        let errors = result.unwrap_err();
        assert!(errors.iter().any(|e| e.message.contains("undefined")));
    }

    #[test]
    fn test_type_mismatch() {
        let result = check_source(r#"
            fn main() {
                let x: int = "hello";
            }
        "#);
        assert!(result.is_err());
        let errors = result.unwrap_err();
        assert!(errors.iter().any(|e| e.message.contains("type mismatch")));
    }

    #[test]
    fn test_return_type_mismatch() {
        let result = check_source(r#"
            fn foo() -> int {
                return "hello";
            }
        "#);
        assert!(result.is_err());
        let errors = result.unwrap_err();
        assert!(errors.iter().any(|e| e.message.contains("return type")));
    }

    #[test]
    fn test_struct_field_access() {
        let result = check_source(r#"
            struct Point { x: int, y: int }
            fn main() {
                let p: Point = Point { x: 1, y: 2 };
                let a: int = p.x;
            }
        "#);
        assert!(result.is_ok());
    }

    #[test]
    fn test_invalid_field_access() {
        let result = check_source(r#"
            struct Point { x: int, y: int }
            fn main() {
                let p: Point = Point { x: 1, y: 2 };
                let a: int = p.z;
            }
        "#);
        assert!(result.is_err());
        let errors = result.unwrap_err();
        assert!(errors.iter().any(|e| e.message.contains("no field")));
    }

    #[test]
    fn test_function_call_type_check() {
        let result = check_source(r#"
            fn add(a: int, b: int) -> int {
                return a + b;
            }
            fn main() {
                let x: int = add(1, 2);
            }
        "#);
        assert!(result.is_ok());
    }

    #[test]
    fn test_function_wrong_arg_count() {
        let result = check_source(r#"
            fn add(a: int, b: int) -> int {
                return a + b;
            }
            fn main() {
                let x: int = add(1);
            }
        "#);
        assert!(result.is_err());
        let errors = result.unwrap_err();
        assert!(errors.iter().any(|e| e.message.contains("arguments")));
    }
}
