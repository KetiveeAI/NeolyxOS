// REOX Compiler - Parser
// Recursive descent parser with Pratt parsing for expressions
// Zero external dependencies

mod ast;

pub use ast::*;

use crate::lexer::{Token, TokenKind, Span};

/// Parser error
#[derive(Debug, Clone)]
pub struct ParseError {
    pub message: String,
    pub span: Span,
}

impl ParseError {
    pub fn new(message: impl Into<String>, span: Span) -> Self {
        Self {
            message: message.into(),
            span,
        }
    }

    pub fn display(&self) -> String {
        format!(
            "error[{}:{}]: {}",
            self.span.line, self.span.column, self.message
        )
    }
}

/// REOX Parser
pub struct Parser<'a> {
    tokens: &'a [Token],
    current: usize,
}

impl<'a> Parser<'a> {
    pub fn new(tokens: &'a [Token]) -> Self {
        Self { tokens, current: 0 }
    }

    // === Utility Methods ===

    fn peek(&self) -> &Token {
        self.tokens.get(self.current).unwrap_or_else(|| {
            self.tokens.last().expect("token stream should have EOF")
        })
    }

    fn peek_kind(&self) -> &TokenKind {
        &self.peek().kind
    }

    fn is_at_end(&self) -> bool {
        matches!(self.peek_kind(), TokenKind::Eof)
    }

    fn advance(&mut self) -> &Token {
        if !self.is_at_end() {
            self.current += 1;
        }
        self.tokens.get(self.current - 1).unwrap()
    }

    fn check(&self, kind: &TokenKind) -> bool {
        std::mem::discriminant(self.peek_kind()) == std::mem::discriminant(kind)
    }

    fn consume(&mut self, kind: &TokenKind, msg: &str) -> Result<&Token, ParseError> {
        if self.check(kind) {
            Ok(self.advance())
        } else {
            Err(ParseError::new(
                format!("{}, found {:?}", msg, self.peek_kind()),
                self.peek().span,
            ))
        }
    }

    fn match_token(&mut self, kinds: &[TokenKind]) -> bool {
        for kind in kinds {
            if self.check(kind) {
                self.advance();
                return true;
            }
        }
        false
    }

    // === Parsing Methods ===

    pub fn parse_program(&mut self) -> Result<Program, ParseError> {
        let mut declarations = Vec::new();

        while !self.is_at_end() {
            declarations.push(self.parse_declaration()?);
        }

        Ok(Program { declarations })
    }

    fn parse_declaration(&mut self) -> Result<Decl, ParseError> {
        match self.peek_kind() {
            TokenKind::Fn => self.parse_fn_decl().map(Decl::Function),
            TokenKind::Struct => self.parse_struct_decl().map(Decl::Struct),
            TokenKind::Import => self.parse_import_decl().map(Decl::Import),
            TokenKind::Extern => self.parse_extern_decl().map(Decl::Extern),
            _ => Err(ParseError::new(
                format!("expected declaration, found {:?}", self.peek_kind()),
                self.peek().span,
            )),
        }
    }

    fn parse_fn_decl(&mut self) -> Result<FnDecl, ParseError> {
        let start_span = self.peek().span;
        self.consume(&TokenKind::Fn, "expected 'fn'")?;

        let name = self.parse_identifier()?;
        self.consume(&TokenKind::LParen, "expected '(' after function name")?;

        let params = self.parse_param_list()?;
        self.consume(&TokenKind::RParen, "expected ')' after parameters")?;

        let return_type = if self.match_token(&[TokenKind::Arrow]) {
            Some(self.parse_type()?)
        } else {
            None
        };

        let body = self.parse_block()?;

        Ok(FnDecl {
            name,
            params,
            return_type,
            body,
            span: start_span,
        })
    }

    fn parse_param_list(&mut self) -> Result<Vec<Param>, ParseError> {
        let mut params = Vec::new();

        if !self.check(&TokenKind::RParen) {
            loop {
                params.push(self.parse_param()?);
                if !self.match_token(&[TokenKind::Comma]) {
                    break;
                }
            }
        }

        Ok(params)
    }

    fn parse_param(&mut self) -> Result<Param, ParseError> {
        let span = self.peek().span;
        let name = self.parse_identifier()?;
        self.consume(&TokenKind::Colon, "expected ':' after parameter name")?;
        let ty = self.parse_type()?;

        Ok(Param { name, ty, span })
    }

    fn parse_struct_decl(&mut self) -> Result<StructDecl, ParseError> {
        let start_span = self.peek().span;
        self.consume(&TokenKind::Struct, "expected 'struct'")?;

        let name = self.parse_identifier()?;
        self.consume(&TokenKind::LBrace, "expected '{'")?;

        let mut fields = Vec::new();
        while !self.check(&TokenKind::RBrace) && !self.is_at_end() {
            fields.push(self.parse_field()?);
            if !self.match_token(&[TokenKind::Comma]) {
                break;
            }
        }

        self.consume(&TokenKind::RBrace, "expected '}'")?;

        Ok(StructDecl {
            name,
            fields,
            span: start_span,
        })
    }

    fn parse_field(&mut self) -> Result<Field, ParseError> {
        let span = self.peek().span;
        let name = self.parse_identifier()?;
        self.consume(&TokenKind::Colon, "expected ':' after field name")?;
        let ty = self.parse_type()?;

        Ok(Field { name, ty, span })
    }

    fn parse_import_decl(&mut self) -> Result<ImportDecl, ParseError> {
        let span = self.peek().span;
        self.consume(&TokenKind::Import, "expected 'import'")?;

        let mut path = vec![self.parse_identifier()?];
        while self.match_token(&[TokenKind::Colon]) {
            self.consume(&TokenKind::Colon, "expected '::'")?;
            path.push(self.parse_identifier()?);
        }

        self.consume(&TokenKind::Semicolon, "expected ';' after import")?;

        Ok(ImportDecl { path, span })
    }

    fn parse_extern_decl(&mut self) -> Result<ExternDecl, ParseError> {
        let span = self.peek().span;
        self.consume(&TokenKind::Extern, "expected 'extern'")?;
        self.consume(&TokenKind::Fn, "expected 'fn' after 'extern'")?;

        let name = self.parse_identifier()?;
        self.consume(&TokenKind::LParen, "expected '('")?;
        let params = self.parse_param_list()?;
        self.consume(&TokenKind::RParen, "expected ')'")?;

        let return_type = if self.match_token(&[TokenKind::Arrow]) {
            Some(self.parse_type()?)
        } else {
            None
        };

        self.consume(&TokenKind::Semicolon, "expected ';'")?;

        Ok(ExternDecl {
            name,
            params,
            return_type,
            span,
        })
    }

    fn parse_type(&mut self) -> Result<Type, ParseError> {
        match self.peek_kind().clone() {
            TokenKind::Int => {
                self.advance();
                Ok(Type::Int)
            }
            TokenKind::Float => {
                self.advance();
                Ok(Type::Float)
            }
            TokenKind::String => {
                self.advance();
                Ok(Type::String)
            }
            TokenKind::Bool => {
                self.advance();
                Ok(Type::Bool)
            }
            TokenKind::Void => {
                self.advance();
                Ok(Type::Void)
            }
            TokenKind::Ident(name) => {
                self.advance();
                Ok(Type::Named(name))
            }
            TokenKind::LBracket => {
                self.advance();
                let inner = self.parse_type()?;
                self.consume(&TokenKind::RBracket, "expected ']'")?;
                Ok(Type::Array(Box::new(inner)))
            }
            _ => Err(ParseError::new(
                format!("expected type, found {:?}", self.peek_kind()),
                self.peek().span,
            )),
        }
    }

    fn parse_block(&mut self) -> Result<Block, ParseError> {
        let span = self.peek().span;
        self.consume(&TokenKind::LBrace, "expected '{'")?;

        let mut statements = Vec::new();
        while !self.check(&TokenKind::RBrace) && !self.is_at_end() {
            statements.push(self.parse_statement()?);
        }

        self.consume(&TokenKind::RBrace, "expected '}'")?;

        Ok(Block { statements, span })
    }

    fn parse_statement(&mut self) -> Result<Stmt, ParseError> {
        match self.peek_kind() {
            TokenKind::Let => self.parse_let_stmt(),
            TokenKind::Return => self.parse_return_stmt(),
            TokenKind::If => self.parse_if_stmt(),
            TokenKind::While => self.parse_while_stmt(),
            TokenKind::For => self.parse_for_stmt(),
            TokenKind::LBrace => Ok(Stmt::Block(self.parse_block()?)),
            _ => self.parse_expr_stmt(),
        }
    }

    fn parse_let_stmt(&mut self) -> Result<Stmt, ParseError> {
        let span = self.peek().span;
        self.consume(&TokenKind::Let, "expected 'let'")?;

        let mutable = self.match_token(&[TokenKind::Mut]);
        let name = self.parse_identifier()?;

        let ty = if self.match_token(&[TokenKind::Colon]) {
            Some(self.parse_type()?)
        } else {
            None
        };

        let init = if self.match_token(&[TokenKind::Eq]) {
            Some(self.parse_expression()?)
        } else {
            None
        };

        self.consume(&TokenKind::Semicolon, "expected ';' after variable declaration")?;

        Ok(Stmt::Let(LetStmt {
            name,
            mutable,
            ty,
            init,
            span,
        }))
    }

    fn parse_return_stmt(&mut self) -> Result<Stmt, ParseError> {
        let span = self.peek().span;
        self.consume(&TokenKind::Return, "expected 'return'")?;

        let value = if !self.check(&TokenKind::Semicolon) {
            Some(self.parse_expression()?)
        } else {
            None
        };

        self.consume(&TokenKind::Semicolon, "expected ';' after return")?;

        Ok(Stmt::Return(ReturnStmt { value, span }))
    }

    fn parse_if_stmt(&mut self) -> Result<Stmt, ParseError> {
        let span = self.peek().span;
        self.consume(&TokenKind::If, "expected 'if'")?;

        let condition = self.parse_expression()?;
        let then_block = self.parse_block()?;

        let else_block = if self.match_token(&[TokenKind::Else]) {
            Some(self.parse_block()?)
        } else {
            None
        };

        Ok(Stmt::If(IfStmt {
            condition,
            then_block,
            else_block,
            span,
        }))
    }

    fn parse_while_stmt(&mut self) -> Result<Stmt, ParseError> {
        let span = self.peek().span;
        self.consume(&TokenKind::While, "expected 'while'")?;

        let condition = self.parse_expression()?;
        let body = self.parse_block()?;

        Ok(Stmt::While(WhileStmt {
            condition,
            body,
            span,
        }))
    }

    fn parse_for_stmt(&mut self) -> Result<Stmt, ParseError> {
        let span = self.peek().span;
        self.consume(&TokenKind::For, "expected 'for'")?;

        let var = self.parse_identifier()?;
        self.consume(&TokenKind::In, "expected 'in'")?;
        let iterable = self.parse_expression()?;
        let body = self.parse_block()?;

        Ok(Stmt::For(ForStmt {
            var,
            iterable,
            body,
            span,
        }))
    }

    fn parse_expr_stmt(&mut self) -> Result<Stmt, ParseError> {
        let expr = self.parse_expression()?;
        self.consume(&TokenKind::Semicolon, "expected ';' after expression")?;
        Ok(Stmt::Expr(expr))
    }

    // === Expression Parsing (Pratt Parser) ===

    fn parse_expression(&mut self) -> Result<Expr, ParseError> {
        self.parse_assignment()
    }

    fn parse_assignment(&mut self) -> Result<Expr, ParseError> {
        let expr = self.parse_or()?;

        if self.match_token(&[TokenKind::Eq]) {
            let span = self.peek().span;
            let value = self.parse_assignment()?;
            return Ok(Expr::Assign(Box::new(expr), Box::new(value), span));
        }

        Ok(expr)
    }

    fn parse_or(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_and()?;

        while self.match_token(&[TokenKind::Or]) {
            let span = self.peek().span;
            let right = self.parse_and()?;
            left = Expr::Binary(Box::new(left), BinOp::Or, Box::new(right), span);
        }

        Ok(left)
    }

    fn parse_and(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_equality()?;

        while self.match_token(&[TokenKind::And]) {
            let span = self.peek().span;
            let right = self.parse_equality()?;
            left = Expr::Binary(Box::new(left), BinOp::And, Box::new(right), span);
        }

        Ok(left)
    }

    fn parse_equality(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_comparison()?;

        while self.match_token(&[TokenKind::EqEq, TokenKind::BangEq]) {
            let op = match self.tokens.get(self.current - 1).map(|t| &t.kind) {
                Some(TokenKind::EqEq) => BinOp::Eq,
                Some(TokenKind::BangEq) => BinOp::Ne,
                _ => unreachable!(),
            };
            let span = self.peek().span;
            let right = self.parse_comparison()?;
            left = Expr::Binary(Box::new(left), op, Box::new(right), span);
        }

        Ok(left)
    }

    fn parse_comparison(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_term()?;

        while self.match_token(&[TokenKind::Lt, TokenKind::Gt, TokenKind::LtEq, TokenKind::GtEq]) {
            let op = match self.tokens.get(self.current - 1).map(|t| &t.kind) {
                Some(TokenKind::Lt) => BinOp::Lt,
                Some(TokenKind::Gt) => BinOp::Gt,
                Some(TokenKind::LtEq) => BinOp::Le,
                Some(TokenKind::GtEq) => BinOp::Ge,
                _ => unreachable!(),
            };
            let span = self.peek().span;
            let right = self.parse_term()?;
            left = Expr::Binary(Box::new(left), op, Box::new(right), span);
        }

        Ok(left)
    }

    fn parse_term(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_factor()?;

        while self.match_token(&[TokenKind::Plus, TokenKind::Minus]) {
            let op = match self.tokens.get(self.current - 1).map(|t| &t.kind) {
                Some(TokenKind::Plus) => BinOp::Add,
                Some(TokenKind::Minus) => BinOp::Sub,
                _ => unreachable!(),
            };
            let span = self.peek().span;
            let right = self.parse_factor()?;
            left = Expr::Binary(Box::new(left), op, Box::new(right), span);
        }

        Ok(left)
    }

    fn parse_factor(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_unary()?;

        while self.match_token(&[TokenKind::Star, TokenKind::Slash, TokenKind::Percent]) {
            let op = match self.tokens.get(self.current - 1).map(|t| &t.kind) {
                Some(TokenKind::Star) => BinOp::Mul,
                Some(TokenKind::Slash) => BinOp::Div,
                Some(TokenKind::Percent) => BinOp::Mod,
                _ => unreachable!(),
            };
            let span = self.peek().span;
            let right = self.parse_unary()?;
            left = Expr::Binary(Box::new(left), op, Box::new(right), span);
        }

        Ok(left)
    }

    fn parse_unary(&mut self) -> Result<Expr, ParseError> {
        if self.match_token(&[TokenKind::Minus, TokenKind::Bang]) {
            let op = match self.tokens.get(self.current - 1).map(|t| &t.kind) {
                Some(TokenKind::Minus) => UnaryOp::Neg,
                Some(TokenKind::Bang) => UnaryOp::Not,
                _ => unreachable!(),
            };
            let span = self.peek().span;
            let right = self.parse_unary()?;
            return Ok(Expr::Unary(op, Box::new(right), span));
        }

        self.parse_call()
    }

    fn parse_call(&mut self) -> Result<Expr, ParseError> {
        let mut expr = self.parse_primary()?;

        loop {
            if self.match_token(&[TokenKind::LParen]) {
                let span = self.peek().span;
                let args = self.parse_arg_list()?;
                self.consume(&TokenKind::RParen, "expected ')' after arguments")?;
                expr = Expr::Call(Box::new(expr), args, span);
            } else if self.match_token(&[TokenKind::Dot]) {
                let span = self.peek().span;
                let name = self.parse_identifier()?;
                expr = Expr::Member(Box::new(expr), name, span);
            } else if self.match_token(&[TokenKind::LBracket]) {
                let span = self.peek().span;
                let index = self.parse_expression()?;
                self.consume(&TokenKind::RBracket, "expected ']'")?;
                expr = Expr::Index(Box::new(expr), Box::new(index), span);
            } else {
                break;
            }
        }

        Ok(expr)
    }

    fn parse_arg_list(&mut self) -> Result<Vec<Expr>, ParseError> {
        let mut args = Vec::new();

        if !self.check(&TokenKind::RParen) {
            loop {
                args.push(self.parse_expression()?);
                if !self.match_token(&[TokenKind::Comma]) {
                    break;
                }
            }
        }

        Ok(args)
    }

    fn parse_primary(&mut self) -> Result<Expr, ParseError> {
        let token = self.peek().clone();

        match &token.kind {
            TokenKind::IntLit(n) => {
                let n = *n;
                self.advance();
                Ok(Expr::Literal(Literal::Int(n, token.span)))
            }
            TokenKind::FloatLit(n) => {
                let n = *n;
                self.advance();
                Ok(Expr::Literal(Literal::Float(n, token.span)))
            }
            TokenKind::StringLit(s) => {
                let s = s.clone();
                self.advance();
                Ok(Expr::Literal(Literal::String(s, token.span)))
            }
            TokenKind::True => {
                self.advance();
                Ok(Expr::Literal(Literal::Bool(true, token.span)))
            }
            TokenKind::False => {
                self.advance();
                Ok(Expr::Literal(Literal::Bool(false, token.span)))
            }
            TokenKind::Ident(name) => {
                let name = name.clone();
                self.advance();

                // Check for struct literal
                if self.check(&TokenKind::LBrace) {
                    self.advance();
                    let mut fields = Vec::new();
                    while !self.check(&TokenKind::RBrace) && !self.is_at_end() {
                        let field_name = self.parse_identifier()?;
                        self.consume(&TokenKind::Colon, "expected ':'")?;
                        let value = self.parse_expression()?;
                        fields.push((field_name, value));
                        if !self.match_token(&[TokenKind::Comma]) {
                            break;
                        }
                    }
                    self.consume(&TokenKind::RBrace, "expected '}'")?;
                    return Ok(Expr::StructLit(name, fields, token.span));
                }

                Ok(Expr::Identifier(name, token.span))
            }
            TokenKind::LParen => {
                self.advance();
                let expr = self.parse_expression()?;
                self.consume(&TokenKind::RParen, "expected ')'")?;
                Ok(expr)
            }
            TokenKind::LBracket => {
                self.advance();
                let mut elements = Vec::new();
                while !self.check(&TokenKind::RBracket) && !self.is_at_end() {
                    elements.push(self.parse_expression()?);
                    if !self.match_token(&[TokenKind::Comma]) {
                        break;
                    }
                }
                self.consume(&TokenKind::RBracket, "expected ']'")?;
                Ok(Expr::ArrayLit(elements, token.span))
            }
            _ => Err(ParseError::new(
                format!("expected expression, found {:?}", token.kind),
                token.span,
            )),
        }
    }

    fn parse_identifier(&mut self) -> Result<String, ParseError> {
        match self.peek_kind().clone() {
            TokenKind::Ident(name) => {
                self.advance();
                Ok(name)
            }
            _ => Err(ParseError::new(
                format!("expected identifier, found {:?}", self.peek_kind()),
                self.peek().span,
            )),
        }
    }
}

/// Convenience type for backward compatibility
pub type Ast = Program;

/// Parse tokens into AST
pub fn parse(tokens: &[Token]) -> Ast {
    let mut parser = Parser::new(tokens);
    match parser.parse_program() {
        Ok(program) => program,
        Err(e) => {
            eprintln!("{}", e.display());
            Program { declarations: vec![] }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::tokenize;

    #[test]
    fn test_parse_simple_function() {
        let tokens = tokenize("fn main() { }").unwrap();
        let ast = parse(&tokens);
        assert_eq!(ast.declarations.len(), 1);
        match &ast.declarations[0] {
            Decl::Function(f) => {
                assert_eq!(f.name, "main");
                assert!(f.params.is_empty());
            }
            _ => panic!("expected function"),
        }
    }

    #[test]
    fn test_parse_function_with_params() {
        let tokens = tokenize("fn add(a: int, b: int) -> int { return a + b; }").unwrap();
        let ast = parse(&tokens);
        assert_eq!(ast.declarations.len(), 1);
        match &ast.declarations[0] {
            Decl::Function(f) => {
                assert_eq!(f.name, "add");
                assert_eq!(f.params.len(), 2);
                assert_eq!(f.return_type, Some(Type::Int));
            }
            _ => panic!("expected function"),
        }
    }

    #[test]
    fn test_parse_struct() {
        let tokens = tokenize("struct Point { x: int, y: int }").unwrap();
        let ast = parse(&tokens);
        assert_eq!(ast.declarations.len(), 1);
        match &ast.declarations[0] {
            Decl::Struct(s) => {
                assert_eq!(s.name, "Point");
                assert_eq!(s.fields.len(), 2);
            }
            _ => panic!("expected struct"),
        }
    }

    #[test]
    fn test_parse_let_statement() {
        let tokens = tokenize("fn main() { let x: int = 42; }").unwrap();
        let ast = parse(&tokens);
        match &ast.declarations[0] {
            Decl::Function(f) => {
                assert_eq!(f.body.statements.len(), 1);
                match &f.body.statements[0] {
                    Stmt::Let(l) => {
                        assert_eq!(l.name, "x");
                        assert_eq!(l.ty, Some(Type::Int));
                    }
                    _ => panic!("expected let"),
                }
            }
            _ => panic!("expected function"),
        }
    }

    #[test]
    fn test_parse_if_statement() {
        let tokens = tokenize("fn main() { if x > 0 { } else { } }").unwrap();
        let ast = parse(&tokens);
        match &ast.declarations[0] {
            Decl::Function(f) => {
                assert_eq!(f.body.statements.len(), 1);
                match &f.body.statements[0] {
                    Stmt::If(i) => {
                        assert!(i.else_block.is_some());
                    }
                    _ => panic!("expected if"),
                }
            }
            _ => panic!("expected function"),
        }
    }

    #[test]
    fn test_parse_expression_precedence() {
        let tokens = tokenize("fn main() { let x = 1 + 2 * 3; }").unwrap();
        let ast = parse(&tokens);
        // 1 + (2 * 3) due to precedence
        match &ast.declarations[0] {
            Decl::Function(f) => {
                match &f.body.statements[0] {
                    Stmt::Let(l) => {
                        match l.init.as_ref().unwrap() {
                            Expr::Binary(_, BinOp::Add, right, _) => {
                                match right.as_ref() {
                                    Expr::Binary(_, BinOp::Mul, _, _) => {}
                                    _ => panic!("expected mul on right"),
                                }
                            }
                            _ => panic!("expected binary add"),
                        }
                    }
                    _ => panic!("expected let"),
                }
            }
            _ => panic!("expected function"),
        }
    }

    #[test]
    fn test_parse_function_call() {
        let tokens = tokenize("fn main() { print(42); }").unwrap();
        let ast = parse(&tokens);
        match &ast.declarations[0] {
            Decl::Function(f) => {
                match &f.body.statements[0] {
                    Stmt::Expr(Expr::Call(callee, args, _)) => {
                        match callee.as_ref() {
                            Expr::Identifier(name, _) => assert_eq!(name, "print"),
                            _ => panic!("expected identifier"),
                        }
                        assert_eq!(args.len(), 1);
                    }
                    _ => panic!("expected call"),
                }
            }
            _ => panic!("expected function"),
        }
    }
}
