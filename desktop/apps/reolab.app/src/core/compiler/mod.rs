pub mod lexer;
pub mod parser;
pub mod ast;
pub mod codegen;
pub mod error;

use std::path::Path;
use anyhow::Result;
use crate::core::compiler::error::CompileError;

pub struct Compiler {
    source_path: String,
    output_path: String,
    optimization_level: u8,
}

impl Compiler {
    pub fn new(source_path: &str) -> Self {
        Self {
            source_path: source_path.to_string(),
            output_path: source_path.replace(".reox", ""),
            optimization_level: 0,
        }
    }

    pub fn with_output(mut self, output_path: &str) -> Self {
        self.output_path = output_path.to_string();
        self
    }

    pub fn with_optimization(mut self, level: u8) -> Self {
        self.optimization_level = level;
        self
    }

    pub fn compile(&self) -> Result<(), CompileError> {
        // Step 1: Lexical Analysis
        let tokens = self.lexical_analysis()?;
        
        // Step 2: Syntax Analysis
        let ast = self.syntax_analysis(&tokens)?;
        
        // Step 3: Semantic Analysis
        self.semantic_analysis(&ast)?;
        
        // Step 4: Code Generation
        self.code_generation(&ast)?;
        
        Ok(())
    }

    fn lexical_analysis(&self) -> Result<Vec<lexer::Token>, CompileError> {
        let source = std::fs::read_to_string(&self.source_path)
            .map_err(|e| CompileError::FileError(e.to_string()))?;
        
        let mut lexer = lexer::Lexer::new(&source);
        lexer.tokenize()
    }

    fn syntax_analysis(&self, tokens: &[lexer::Token]) -> Result<ast::Program, CompileError> {
        let mut parser = parser::Parser::new(tokens);
        parser.parse()
    }

    fn semantic_analysis(&self, ast: &ast::Program) -> Result<(), CompileError> {
        // Type checking, symbol resolution, etc.
        Ok(())
    }

    fn code_generation(&self, ast: &ast::Program) -> Result<(), CompileError> {
        let mut codegen = codegen::CodeGenerator::new(&self.output_path);
        codegen.generate(ast)
    }
}

pub struct CompilerConfig {
    pub source_path: String,
    pub output_path: Option<String>,
    pub optimization_level: u8,
    pub debug_info: bool,
    pub warnings_as_errors: bool,
}

impl Default for CompilerConfig {
    fn default() -> Self {
        Self {
            source_path: String::new(),
            output_path: None,
            optimization_level: 0,
            debug_info: false,
            warnings_as_errors: false,
        }
    }
}

pub fn compile_file(config: CompilerConfig) -> Result<(), CompileError> {
    let mut compiler = Compiler::new(&config.source_path);
    
    if let Some(output) = config.output_path {
        compiler = compiler.with_output(&output);
    }
    
    compiler = compiler.with_optimization(config.optimization_level);
    
    compiler.compile()
} 