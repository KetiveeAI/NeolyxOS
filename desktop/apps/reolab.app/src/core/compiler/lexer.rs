use crate::core::compiler::error::{CompileError, CompileWarning};

#[derive(Debug, Clone, PartialEq)]
pub enum TokenType {
    // Keywords
    Let,
    Fn,
    If,
    Else,
    While,
    For,
    Return,
    Import,
    Struct,
    Enum,
    Match,
    Try,
    Catch,
    Async,
    Await,
    Spawn,
    Channel,
    
    // Types
    Int,
    Float,
    Bool,
    String,
    Char,
    Array,
    Map,
    Any,
    Result,
    Error,
    
    // OS Keywords
    Kernel,
    Syscall,
    Hardware,
    
    // Identifiers and literals
    Identifier(String),
    IntegerLiteral(i64),
    FloatLiteral(f64),
    StringLiteral(String),
    CharLiteral(char),
    BooleanLiteral(bool),
    
    // Operators
    Plus,
    Minus,
    Multiply,
    Divide,
    Modulo,
    Assign,
    PlusAssign,
    MinusAssign,
    MultiplyAssign,
    DivideAssign,
    Equal,
    NotEqual,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,
    LogicalAnd,
    LogicalOr,
    LogicalNot,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    BitwiseNot,
    LeftShift,
    RightShift,
    
    // Delimiters
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Semicolon,
    Comma,
    Dot,
    Arrow,
    Colon,
    DoubleColon,
    
    // Special
    EndOfFile,
    Unknown,
}

#[derive(Debug, Clone)]
pub struct Token {
    pub token_type: TokenType,
    pub lexeme: String,
    pub line: usize,
    pub column: usize,
}

pub struct Lexer {
    source: Vec<char>,
    current: usize,
    start: usize,
    line: usize,
    column: usize,
    tokens: Vec<Token>,
    warnings: Vec<CompileWarning>,
}

impl Lexer {
    pub fn new(source: &str) -> Self {
        Self {
            source: source.chars().collect(),
            current: 0,
            start: 0,
            line: 1,
            column: 1,
            tokens: Vec::new(),
            warnings: Vec::new(),
        }
    }

    pub fn tokenize(&mut self) -> Result<Vec<Token>, CompileError> {
        while !self.is_at_end() {
            self.start = self.current;
            self.scan_token()?;
        }

        self.tokens.push(Token {
            token_type: TokenType::EndOfFile,
            lexeme: String::new(),
            line: self.line,
            column: self.column,
        });

        Ok(self.tokens.clone())
    }

    fn scan_token(&mut self) -> Result<(), CompileError> {
        let c = self.advance();

        match c {
            '(' => self.add_token(TokenType::LeftParen),
            ')' => self.add_token(TokenType::RightParen),
            '{' => self.add_token(TokenType::LeftBrace),
            '}' => self.add_token(TokenType::RightBrace),
            '[' => self.add_token(TokenType::LeftBracket),
            ']' => self.add_token(TokenType::RightBracket),
            ';' => self.add_token(TokenType::Semicolon),
            ',' => self.add_token(TokenType::Comma),
            '.' => self.add_token(TokenType::Dot),
            ':' => {
                if self.match_char(':') {
                    self.add_token(TokenType::DoubleColon);
                } else {
                    self.add_token(TokenType::Colon);
                }
            }
            '-' => {
                if self.match_char('>') {
                    self.add_token(TokenType::Arrow);
                } else if self.match_char('=') {
                    self.add_token(TokenType::MinusAssign);
                } else {
                    self.add_token(TokenType::Minus);
                }
            }
            '+' => {
                if self.match_char('=') {
                    self.add_token(TokenType::PlusAssign);
                } else {
                    self.add_token(TokenType::Plus);
                }
            }
            '*' => {
                if self.match_char('=') {
                    self.add_token(TokenType::MultiplyAssign);
                } else {
                    self.add_token(TokenType::Multiply);
                }
            }
            '/' => {
                if self.match_char('=') {
                    self.add_token(TokenType::DivideAssign);
                } else if self.match_char('/') {
                    self.comment()?;
                } else if self.match_char('*') {
                    self.multiline_comment()?;
                } else {
                    self.add_token(TokenType::Divide);
                }
            }
            '%' => self.add_token(TokenType::Modulo),
            '=' => {
                if self.match_char('=') {
                    self.add_token(TokenType::Equal);
                } else {
                    self.add_token(TokenType::Assign);
                }
            }
            '!' => {
                if self.match_char('=') {
                    self.add_token(TokenType::NotEqual);
                } else {
                    self.add_token(TokenType::LogicalNot);
                }
            }
            '<' => {
                if self.match_char('=') {
                    self.add_token(TokenType::LessEqual);
                } else if self.match_char('<') {
                    self.add_token(TokenType::LeftShift);
                } else {
                    self.add_token(TokenType::LessThan);
                }
            }
            '>' => {
                if self.match_char('=') {
                    self.add_token(TokenType::GreaterEqual);
                } else if self.match_char('>') {
                    self.add_token(TokenType::RightShift);
                } else {
                    self.add_token(TokenType::GreaterThan);
                }
            }
            '&' => {
                if self.match_char('&') {
                    self.add_token(TokenType::LogicalAnd);
                } else {
                    self.add_token(TokenType::BitwiseAnd);
                }
            }
            '|' => {
                if self.match_char('|') {
                    self.add_token(TokenType::LogicalOr);
                } else {
                    self.add_token(TokenType::BitwiseOr);
                }
            }
            '^' => self.add_token(TokenType::BitwiseXor),
            '~' => self.add_token(TokenType::BitwiseNot),
            '"' => self.string()?,
            '\'' => self.character()?,
            ' ' | '\r' | '\t' => {
                // Ignore whitespace
            }
            '\n' => {
                self.line += 1;
                self.column = 0;
            }
            _ => {
                if c.is_ascii_digit() {
                    self.number()?;
                } else if c.is_ascii_alphabetic() || c == '_' {
                    self.identifier()?;
                } else {
                    return Err(CompileError::LexicalError {
                        line: self.line,
                        column: self.column,
                        message: format!("Unexpected character: {}", c),
                    });
                }
            }
        }

        Ok(())
    }

    fn comment(&mut self) -> Result<(), CompileError> {
        while self.peek() != '\n' && !self.is_at_end() {
            self.advance();
        }
        Ok(())
    }

    fn multiline_comment(&mut self) -> Result<(), CompileError> {
        while !self.is_at_end() {
            if self.peek() == '*' && self.peek_next() == '/' {
                self.advance(); // consume '*'
                self.advance(); // consume '/'
                break;
            }
            if self.peek() == '\n' {
                self.line += 1;
                self.column = 0;
            }
            self.advance();
        }
        Ok(())
    }

    fn string(&mut self) -> Result<(), CompileError> {
        while self.peek() != '"' && !self.is_at_end() {
            if self.peek() == '\n' {
                self.line += 1;
                self.column = 0;
            }
            self.advance();
        }

        if self.is_at_end() {
            return Err(CompileError::LexicalError {
                line: self.line,
                column: self.column,
                message: "Unterminated string".to_string(),
            });
        }

        // Consume the closing "
        self.advance();

        // Trim the surrounding quotes
        let value = self.source[self.start + 1..self.current - 1]
            .iter()
            .collect::<String>();

        self.add_token(TokenType::StringLiteral(value));
        Ok(())
    }

    fn character(&mut self) -> Result<(), CompileError> {
        if self.peek() == '\'' {
            return Err(CompileError::LexicalError {
                line: self.line,
                column: self.column,
                message: "Empty character literal".to_string(),
            });
        }

        let c = self.advance();
        if self.peek() != '\'' {
            return Err(CompileError::LexicalError {
                line: self.line,
                column: self.column,
                message: "Unterminated character literal".to_string(),
            });
        }

        self.advance(); // consume closing '
        self.add_token(TokenType::CharLiteral(c));
        Ok(())
    }

    fn number(&mut self) -> Result<(), CompileError> {
        while self.peek().is_ascii_digit() {
            self.advance();
        }

        // Look for decimal part
        if self.peek() == '.' && self.peek_next().is_ascii_digit() {
            self.advance(); // consume '.'

            while self.peek().is_ascii_digit() {
                self.advance();
            }
        }

        let value = self.source[self.start..self.current]
            .iter()
            .collect::<String>();

        if value.contains('.') {
            let float_value = value.parse::<f64>().map_err(|_| {
                CompileError::LexicalError {
                    line: self.line,
                    column: self.column,
                    message: format!("Invalid float literal: {}", value),
                }
            })?;
            self.add_token(TokenType::FloatLiteral(float_value));
        } else {
            let int_value = value.parse::<i64>().map_err(|_| {
                CompileError::LexicalError {
                    line: self.line,
                    column: self.column,
                    message: format!("Invalid integer literal: {}", value),
                }
            })?;
            self.add_token(TokenType::IntegerLiteral(int_value));
        }

        Ok(())
    }

    fn identifier(&mut self) -> Result<(), CompileError> {
        while self.peek().is_alphanumeric() || self.peek() == '_' {
            self.advance();
        }

        let text = self.source[self.start..self.current]
            .iter()
            .collect::<String>();

        let token_type = match text.as_str() {
            "let" => TokenType::Let,
            "fn" => TokenType::Fn,
            "if" => TokenType::If,
            "else" => TokenType::Else,
            "while" => TokenType::While,
            "for" => TokenType::For,
            "return" => TokenType::Return,
            "import" => TokenType::Import,
            "struct" => TokenType::Struct,
            "enum" => TokenType::Enum,
            "match" => TokenType::Match,
            "try" => TokenType::Try,
            "catch" => TokenType::Catch,
            "async" => TokenType::Async,
            "await" => TokenType::Await,
            "spawn" => TokenType::Spawn,
            "channel" => TokenType::Channel,
            "int" => TokenType::Int,
            "float" => TokenType::Float,
            "bool" => TokenType::Bool,
            "string" => TokenType::String,
            "char" => TokenType::Char,
            "array" => TokenType::Array,
            "map" => TokenType::Map,
            "any" => TokenType::Any,
            "Result" => TokenType::Result,
            "Error" => TokenType::Error,
            "kernel" => TokenType::Kernel,
            "syscall" => TokenType::Syscall,
            "hardware" => TokenType::Hardware,
            "true" => TokenType::BooleanLiteral(true),
            "false" => TokenType::BooleanLiteral(false),
            _ => TokenType::Identifier(text),
        };

        self.add_token(token_type);
        Ok(())
    }

    fn advance(&mut self) -> char {
        let c = self.source[self.current];
        self.current += 1;
        self.column += 1;
        c
    }

    fn peek(&self) -> char {
        if self.is_at_end() {
            '\0'
        } else {
            self.source[self.current]
        }
    }

    fn peek_next(&self) -> char {
        if self.current + 1 >= self.source.len() {
            '\0'
        } else {
            self.source[self.current + 1]
        }
    }

    fn match_char(&mut self, expected: char) -> bool {
        if self.is_at_end() {
            return false;
        }
        if self.source[self.current] != expected {
            return false;
        }

        self.current += 1;
        self.column += 1;
        true
    }

    fn is_at_end(&self) -> bool {
        self.current >= self.source.len()
    }

    fn add_token(&mut self, token_type: TokenType) {
        let lexeme = self.source[self.start..self.current]
            .iter()
            .collect::<String>();

        self.tokens.push(Token {
            token_type,
            lexeme,
            line: self.line,
            column: self.column - lexeme.len(),
        });
    }
} 