use std::fmt;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum CompileError {
    #[error("File error: {0}")]
    FileError(String),
    
    #[error("Lexical error at line {line}, column {column}: {message}")]
    LexicalError {
        line: usize,
        column: usize,
        message: String,
    },
    
    #[error("Syntax error at line {line}, column {column}: {message}")]
    SyntaxError {
        line: usize,
        column: usize,
        message: String,
    },
    
    #[error("Semantic error: {message}")]
    SemanticError {
        message: String,
    },
    
    #[error("Type error: {message}")]
    TypeError {
        message: String,
    },
    
    #[error("Code generation error: {message}")]
    CodeGenError {
        message: String,
    },
    
    #[error("Linker error: {message}")]
    LinkerError {
        message: String,
    },
}

#[derive(Debug, Clone)]
pub struct CompileWarning {
    pub line: usize,
    pub column: usize,
    pub message: String,
    pub warning_type: WarningType,
}

#[derive(Debug, Clone)]
pub enum WarningType {
    UnusedVariable,
    UnusedImport,
    DeprecatedFeature,
    Performance,
    Style,
}

impl fmt::Display for CompileWarning {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Warning at line {}, column {}: {}",
            self.line, self.column, self.message
        )
    }
}

pub struct CompileResult {
    pub success: bool,
    pub errors: Vec<CompileError>,
    pub warnings: Vec<CompileWarning>,
}

impl CompileResult {
    pub fn new() -> Self {
        Self {
            success: true,
            errors: Vec::new(),
            warnings: Vec::new(),
        }
    }

    pub fn add_error(&mut self, error: CompileError) {
        self.success = false;
        self.errors.push(error);
    }

    pub fn add_warning(&mut self, warning: CompileWarning) {
        self.warnings.push(warning);
    }

    pub fn has_errors(&self) -> bool {
        !self.errors.is_empty()
    }

    pub fn has_warnings(&self) -> bool {
        !self.warnings.is_empty()
    }
} 