// REOX Compiler - Lexer
// Transforms source text into tokens
// Zero external dependencies

mod token;

pub use token::{Token, TokenKind, Span};

/// Lexer error
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LexError {
    pub message: String,
    pub line: u32,
    pub column: u32,
}

impl LexError {
    pub fn new(message: impl Into<String>, line: u32, column: u32) -> Self {
        Self {
            message: message.into(),
            line,
            column,
        }
    }

    /// Format error for display
    pub fn display(&self) -> String {
        format!("error[{}:{}]: {}", self.line, self.column, self.message)
    }
}

/// REOX Lexer
pub struct Lexer<'a> {
    source: &'a str,
    chars: std::iter::Peekable<std::str::CharIndices<'a>>,
    line: u32,
    column: u32,
    current_pos: usize,
}

impl<'a> Lexer<'a> {
    pub fn new(source: &'a str) -> Self {
        Self {
            source,
            chars: source.char_indices().peekable(),
            line: 1,
            column: 1,
            current_pos: 0,
        }
    }

    /// Advance to next character
    fn advance(&mut self) -> Option<(usize, char)> {
        if let Some((pos, ch)) = self.chars.next() {
            self.current_pos = pos;
            if ch == '\n' {
                self.line += 1;
                self.column = 1;
            } else {
                self.column += 1;
            }
            Some((pos, ch))
        } else {
            None
        }
    }

    /// Peek at next character without consuming
    fn peek(&mut self) -> Option<char> {
        self.chars.peek().map(|(_, ch)| *ch)
    }

    /// Peek at character after next
    fn peek_next(&self) -> Option<char> {
        let mut iter = self.chars.clone();
        iter.next();
        iter.peek().map(|(_, ch)| *ch)
    }

    /// Skip whitespace and comments
    fn skip_whitespace_and_comments(&mut self) {
        loop {
            match self.peek() {
                Some(' ') | Some('\t') | Some('\r') | Some('\n') => {
                    self.advance();
                }
                Some('/') => {
                    if self.peek_next() == Some('/') {
                        // Line comment
                        self.advance(); // /
                        self.advance(); // /
                        while let Some(ch) = self.peek() {
                            if ch == '\n' {
                                break;
                            }
                            self.advance();
                        }
                    } else if self.peek_next() == Some('*') {
                        // Block comment
                        self.advance(); // /
                        self.advance(); // *
                        let mut depth = 1;
                        while depth > 0 {
                            match self.advance() {
                                Some((_, '*')) if self.peek() == Some('/') => {
                                    self.advance();
                                    depth -= 1;
                                }
                                Some((_, '/')) if self.peek() == Some('*') => {
                                    self.advance();
                                    depth += 1;
                                }
                                None => break,
                                _ => {}
                            }
                        }
                    } else {
                        break;
                    }
                }
                _ => break,
            }
        }
    }

    /// Scan an identifier or keyword
    fn scan_identifier(&mut self, start_pos: usize, start_line: u32, start_col: u32) -> Token {
        let mut end_pos = start_pos;

        while let Some(ch) = self.peek() {
            if ch.is_alphanumeric() || ch == '_' {
                if let Some((pos, _)) = self.advance() {
                    end_pos = pos;
                }
            } else {
                break;
            }
        }

        let text = &self.source[start_pos..=end_pos];
        let span = Span::new(start_line, start_col, start_pos, end_pos + 1);

        let kind = TokenKind::keyword_from_str(text)
            .unwrap_or_else(|| TokenKind::Ident(text.to_string()));

        Token::new(kind, span)
    }

    /// Scan a number literal
    fn scan_number(&mut self, start_pos: usize, start_line: u32, start_col: u32) -> Result<Token, LexError> {
        let mut end_pos = start_pos;
        let mut is_float = false;

        while let Some(ch) = self.peek() {
            if ch.is_ascii_digit() {
                if let Some((pos, _)) = self.advance() {
                    end_pos = pos;
                }
            } else if ch == '.' && !is_float {
                // Check if next char is a digit (not a method call)
                if let Some(next) = self.peek_next() {
                    if next.is_ascii_digit() {
                        is_float = true;
                        if let Some((pos, _)) = self.advance() {
                            end_pos = pos;
                        }
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        let text = &self.source[start_pos..=end_pos];
        let span = Span::new(start_line, start_col, start_pos, end_pos + 1);

        if is_float {
            match text.parse::<f64>() {
                Ok(val) => Ok(Token::new(TokenKind::FloatLit(val), span)),
                Err(_) => Err(LexError::new(
                    format!("invalid float literal: {}", text),
                    start_line,
                    start_col,
                )),
            }
        } else {
            match text.parse::<i64>() {
                Ok(val) => Ok(Token::new(TokenKind::IntLit(val), span)),
                Err(_) => Err(LexError::new(
                    format!("invalid integer literal: {}", text),
                    start_line,
                    start_col,
                )),
            }
        }
    }

    /// Scan a string literal
    fn scan_string(&mut self, start_pos: usize, start_line: u32, start_col: u32) -> Result<Token, LexError> {
        let mut value = String::new();
        let mut end_pos = start_pos;

        loop {
            match self.advance() {
                Some((pos, '"')) => {
                    end_pos = pos;
                    break;
                }
                Some((_, '\\')) => {
                    // Escape sequence
                    match self.advance() {
                        Some((_, 'n')) => value.push('\n'),
                        Some((_, 't')) => value.push('\t'),
                        Some((_, 'r')) => value.push('\r'),
                        Some((_, '\\')) => value.push('\\'),
                        Some((_, '"')) => value.push('"'),
                        Some((_, '0')) => value.push('\0'),
                        Some((_, ch)) => {
                            return Err(LexError::new(
                                format!("invalid escape sequence: \\{}", ch),
                                self.line,
                                self.column,
                            ));
                        }
                        None => {
                            return Err(LexError::new(
                                "unexpected end of file in escape sequence",
                                self.line,
                                self.column,
                            ));
                        }
                    }
                }
                Some((_, '\n')) => {
                    return Err(LexError::new(
                        "unterminated string literal",
                        start_line,
                        start_col,
                    ));
                }
                Some((_, ch)) => {
                    value.push(ch);
                }
                None => {
                    return Err(LexError::new(
                        "unterminated string literal",
                        start_line,
                        start_col,
                    ));
                }
            }
        }

        let span = Span::new(start_line, start_col, start_pos, end_pos + 1);
        Ok(Token::new(TokenKind::StringLit(value), span))
    }

    /// Get next token
    fn next_token(&mut self) -> Result<Token, LexError> {
        self.skip_whitespace_and_comments();

        let start_line = self.line;
        let start_col = self.column;

        match self.advance() {
            None => Ok(Token::eof(self.current_pos)),

            Some((pos, ch)) => {
                let span = Span::new(start_line, start_col, pos, pos + 1);

                match ch {
                    // Single character tokens
                    '(' => Ok(Token::new(TokenKind::LParen, span)),
                    ')' => Ok(Token::new(TokenKind::RParen, span)),
                    '{' => Ok(Token::new(TokenKind::LBrace, span)),
                    '}' => Ok(Token::new(TokenKind::RBrace, span)),
                    '[' => Ok(Token::new(TokenKind::LBracket, span)),
                    ']' => Ok(Token::new(TokenKind::RBracket, span)),
                    ',' => Ok(Token::new(TokenKind::Comma, span)),
                    ';' => Ok(Token::new(TokenKind::Semicolon, span)),
                    ':' => Ok(Token::new(TokenKind::Colon, span)),
                    '.' => Ok(Token::new(TokenKind::Dot, span)),
                    '@' => Ok(Token::new(TokenKind::At, span)),
                    '+' => Ok(Token::new(TokenKind::Plus, span)),
                    '*' => Ok(Token::new(TokenKind::Star, span)),
                    '%' => Ok(Token::new(TokenKind::Percent, span)),

                    // Multi-character operators
                    '-' => {
                        if self.peek() == Some('>') {
                            self.advance();
                            let span = Span::new(start_line, start_col, pos, pos + 2);
                            Ok(Token::new(TokenKind::Arrow, span))
                        } else {
                            Ok(Token::new(TokenKind::Minus, span))
                        }
                    }
                    '/' => Ok(Token::new(TokenKind::Slash, span)),
                    '=' => {
                        if self.peek() == Some('=') {
                            self.advance();
                            let span = Span::new(start_line, start_col, pos, pos + 2);
                            Ok(Token::new(TokenKind::EqEq, span))
                        } else {
                            Ok(Token::new(TokenKind::Eq, span))
                        }
                    }
                    '!' => {
                        if self.peek() == Some('=') {
                            self.advance();
                            let span = Span::new(start_line, start_col, pos, pos + 2);
                            Ok(Token::new(TokenKind::BangEq, span))
                        } else {
                            Ok(Token::new(TokenKind::Bang, span))
                        }
                    }
                    '<' => {
                        if self.peek() == Some('=') {
                            self.advance();
                            let span = Span::new(start_line, start_col, pos, pos + 2);
                            Ok(Token::new(TokenKind::LtEq, span))
                        } else {
                            Ok(Token::new(TokenKind::Lt, span))
                        }
                    }
                    '>' => {
                        if self.peek() == Some('=') {
                            self.advance();
                            let span = Span::new(start_line, start_col, pos, pos + 2);
                            Ok(Token::new(TokenKind::GtEq, span))
                        } else {
                            Ok(Token::new(TokenKind::Gt, span))
                        }
                    }
                    '&' => {
                        if self.peek() == Some('&') {
                            self.advance();
                            let span = Span::new(start_line, start_col, pos, pos + 2);
                            Ok(Token::new(TokenKind::And, span))
                        } else {
                            Err(LexError::new("expected '&&'", start_line, start_col))
                        }
                    }
                    '|' => {
                        if self.peek() == Some('|') {
                            self.advance();
                            let span = Span::new(start_line, start_col, pos, pos + 2);
                            Ok(Token::new(TokenKind::Or, span))
                        } else {
                            Err(LexError::new("expected '||'", start_line, start_col))
                        }
                    }

                    // String literals
                    '"' => self.scan_string(pos, start_line, start_col),

                    // Numbers
                    '0'..='9' => self.scan_number(pos, start_line, start_col),

                    // Identifiers and keywords
                    'a'..='z' | 'A'..='Z' | '_' => {
                        Ok(self.scan_identifier(pos, start_line, start_col))
                    }

                    _ => Err(LexError::new(
                        format!("unexpected character: '{}'", ch),
                        start_line,
                        start_col,
                    )),
                }
            }
        }
    }
}

/// Tokenize source code into a vector of tokens
pub fn tokenize(source: &str) -> Result<Vec<Token>, LexError> {
    let mut lexer = Lexer::new(source);
    let mut tokens = Vec::new();

    loop {
        let token = lexer.next_token()?;
        let is_eof = token.kind == TokenKind::Eof;
        tokens.push(token);
        if is_eof {
            break;
        }
    }

    Ok(tokens)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_empty_source() {
        let tokens = tokenize("").unwrap();
        assert_eq!(tokens.len(), 1);
        assert_eq!(tokens[0].kind, TokenKind::Eof);
    }

    #[test]
    fn test_keywords() {
        let tokens = tokenize("fn let mut if else while for return struct").unwrap();
        assert_eq!(tokens[0].kind, TokenKind::Fn);
        assert_eq!(tokens[1].kind, TokenKind::Let);
        assert_eq!(tokens[2].kind, TokenKind::Mut);
        assert_eq!(tokens[3].kind, TokenKind::If);
        assert_eq!(tokens[4].kind, TokenKind::Else);
        assert_eq!(tokens[5].kind, TokenKind::While);
        assert_eq!(tokens[6].kind, TokenKind::For);
        assert_eq!(tokens[7].kind, TokenKind::Return);
        assert_eq!(tokens[8].kind, TokenKind::Struct);
    }

    #[test]
    fn test_operators() {
        let tokens = tokenize("+ - * / = == != < > <= >= && ||").unwrap();
        assert_eq!(tokens[0].kind, TokenKind::Plus);
        assert_eq!(tokens[1].kind, TokenKind::Minus);
        assert_eq!(tokens[2].kind, TokenKind::Star);
        assert_eq!(tokens[3].kind, TokenKind::Slash);
        assert_eq!(tokens[4].kind, TokenKind::Eq);
        assert_eq!(tokens[5].kind, TokenKind::EqEq);
        assert_eq!(tokens[6].kind, TokenKind::BangEq);
        assert_eq!(tokens[7].kind, TokenKind::Lt);
        assert_eq!(tokens[8].kind, TokenKind::Gt);
        assert_eq!(tokens[9].kind, TokenKind::LtEq);
        assert_eq!(tokens[10].kind, TokenKind::GtEq);
        assert_eq!(tokens[11].kind, TokenKind::And);
        assert_eq!(tokens[12].kind, TokenKind::Or);
    }

    #[test]
    fn test_delimiters() {
        let tokens = tokenize("( ) { } [ ] , ; : . ->").unwrap();
        assert_eq!(tokens[0].kind, TokenKind::LParen);
        assert_eq!(tokens[1].kind, TokenKind::RParen);
        assert_eq!(tokens[2].kind, TokenKind::LBrace);
        assert_eq!(tokens[3].kind, TokenKind::RBrace);
        assert_eq!(tokens[4].kind, TokenKind::LBracket);
        assert_eq!(tokens[5].kind, TokenKind::RBracket);
        assert_eq!(tokens[6].kind, TokenKind::Comma);
        assert_eq!(tokens[7].kind, TokenKind::Semicolon);
        assert_eq!(tokens[8].kind, TokenKind::Colon);
        assert_eq!(tokens[9].kind, TokenKind::Dot);
        assert_eq!(tokens[10].kind, TokenKind::Arrow);
    }

    #[test]
    fn test_integers() {
        let tokens = tokenize("0 42 12345").unwrap();
        assert_eq!(tokens[0].kind, TokenKind::IntLit(0));
        assert_eq!(tokens[1].kind, TokenKind::IntLit(42));
        assert_eq!(tokens[2].kind, TokenKind::IntLit(12345));
    }

    #[test]
    fn test_floats() {
        let tokens = tokenize("3.14 0.5 123.456").unwrap();
        assert_eq!(tokens[0].kind, TokenKind::FloatLit(3.14));
        assert_eq!(tokens[1].kind, TokenKind::FloatLit(0.5));
        assert_eq!(tokens[2].kind, TokenKind::FloatLit(123.456));
    }

    #[test]
    fn test_strings() {
        let tokens = tokenize(r#""hello" "world""#).unwrap();
        assert_eq!(tokens[0].kind, TokenKind::StringLit("hello".to_string()));
        assert_eq!(tokens[1].kind, TokenKind::StringLit("world".to_string()));
    }

    #[test]
    fn test_string_escapes() {
        let tokens = tokenize(r#""hello\nworld\ttab""#).unwrap();
        assert_eq!(tokens[0].kind, TokenKind::StringLit("hello\nworld\ttab".to_string()));
    }

    #[test]
    fn test_identifiers() {
        let tokens = tokenize("foo bar_baz _private camelCase").unwrap();
        assert_eq!(tokens[0].kind, TokenKind::Ident("foo".to_string()));
        assert_eq!(tokens[1].kind, TokenKind::Ident("bar_baz".to_string()));
        assert_eq!(tokens[2].kind, TokenKind::Ident("_private".to_string()));
        assert_eq!(tokens[3].kind, TokenKind::Ident("camelCase".to_string()));
    }

    #[test]
    fn test_line_comments() {
        let tokens = tokenize("foo // comment\nbar").unwrap();
        assert_eq!(tokens[0].kind, TokenKind::Ident("foo".to_string()));
        assert_eq!(tokens[1].kind, TokenKind::Ident("bar".to_string()));
    }

    #[test]
    fn test_block_comments() {
        let tokens = tokenize("foo /* comment */ bar").unwrap();
        assert_eq!(tokens[0].kind, TokenKind::Ident("foo".to_string()));
        assert_eq!(tokens[1].kind, TokenKind::Ident("bar".to_string()));
    }

    #[test]
    fn test_full_function() {
        let source = r#"
            fn add(a: int, b: int) -> int {
                return a + b;
            }
        "#;
        let tokens = tokenize(source).unwrap();

        assert_eq!(tokens[0].kind, TokenKind::Fn);
        assert_eq!(tokens[1].kind, TokenKind::Ident("add".to_string()));
        assert_eq!(tokens[2].kind, TokenKind::LParen);
        assert_eq!(tokens[3].kind, TokenKind::Ident("a".to_string()));
        assert_eq!(tokens[4].kind, TokenKind::Colon);
        assert_eq!(tokens[5].kind, TokenKind::Int);
    }

    #[test]
    fn test_struct_definition() {
        let source = r#"
            struct Point {
                x: int,
                y: int
            }
        "#;
        let tokens = tokenize(source).unwrap();

        assert_eq!(tokens[0].kind, TokenKind::Struct);
        assert_eq!(tokens[1].kind, TokenKind::Ident("Point".to_string()));
        assert_eq!(tokens[2].kind, TokenKind::LBrace);
    }

    #[test]
    fn test_error_unterminated_string() {
        let result = tokenize("\"unterminated");
        assert!(result.is_err());
    }
}
