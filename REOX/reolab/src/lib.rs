// REOX Compiler Library
// Zero external dependencies

pub mod lexer;
pub mod parser;
pub mod typechecker;
pub mod codegen;
pub mod profiler;
pub mod interpreter;

// Re-export main types for convenience
pub use lexer::{Token, TokenKind, Span, tokenize, LexError};
pub use parser::{Ast, parse};
pub use typechecker::check;
pub use codegen::generate;
pub use interpreter::{Interpreter, Value, eval};

