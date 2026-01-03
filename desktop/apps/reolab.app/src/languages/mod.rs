pub mod c;
pub mod cpp;
pub mod objc;
pub mod reox;
pub mod rust;
pub mod perl;
pub mod common;

use std::path::PathBuf;
use anyhow::Result;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Language {
    C,
    Cpp,
    ObjectiveC,
    ReoX,
    Rust,
    Perl,
}

impl Language {
    pub fn from_extension(ext: &str) -> Option<Self> {
        match ext.to_lowercase().as_str() {
            "c" => Some(Language::C),
            "cpp" | "cc" | "cxx" | "c++" => Some(Language::Cpp),
            "m" | "mm" => Some(Language::ObjectiveC),
            "reox" => Some(Language::ReoX),
            "rs" => Some(Language::Rust),
            "pl" | "pm" => Some(Language::Perl),
            _ => None,
        }
    }
    
    pub fn get_extensions(&self) -> Vec<&'static str> {
        match self {
            Language::C => vec!["c", "h"],
            Language::Cpp => vec!["cpp", "cc", "cxx", "hpp", "hh", "hxx"],
            Language::ObjectiveC => vec!["m", "mm", "h"],
            Language::ReoX => vec!["reox", "reoxh"],
            Language::Rust => vec!["rs"],
            Language::Perl => vec!["pl", "pm"],
        }
    }
    
    pub fn get_compiler(&self) -> &'static str {
        match self {
            Language::C => "gcc",
            Language::Cpp => "g++",
            Language::ObjectiveC => "clang",
            Language::ReoX => "reoxc",
            Language::Rust => "cargo",
            Language::Perl => "perl",
        }
    }
    
    pub fn get_icon(&self) -> &'static str {
        match self {
            Language::C => "icons/c.png",
            Language::Cpp => "icons/cpp.png",
            Language::ObjectiveC => "icons/objc.png",
            Language::ReoX => "icons/reox.png",
            Language::Rust => "icons/rust.png",
            Language::Perl => "icons/perl.png",
        }
    }
}

pub trait LanguageSupport {
    fn compile(&self, source_file: &PathBuf, output_file: &PathBuf, config: &LanguageConfig) -> Result<()>;
    fn run(&self, executable: &PathBuf, args: &[String]) -> Result<()>;
    fn format(&self, source_file: &PathBuf) -> Result<()>;
    fn lint(&self, source_file: &PathBuf) -> Result<Vec<LintMessage>>;
    fn get_syntax_highlighter(&self) -> Box<dyn SyntaxHighlighter>;
}

pub trait SyntaxHighlighter {
    fn highlight(&self, source: &str) -> Vec<Token>;
}

#[derive(Debug, Clone)]
pub struct Token {
    pub token_type: TokenType,
    pub start: usize,
    pub end: usize,
    pub text: String,
}

#[derive(Debug, Clone, PartialEq)]
pub enum TokenType {
    Keyword,
    Identifier,
    String,
    Number,
    Comment,
    Operator,
    Delimiter,
    Preprocessor,
    Type,
    Function,
    Variable,
    Constant,
}

#[derive(Debug, Clone)]
pub struct LintMessage {
    pub level: LintLevel,
    pub message: String,
    pub line: usize,
    pub column: usize,
    pub file: PathBuf,
}

#[derive(Debug, Clone)]
pub enum LintLevel {
    Error,
    Warning,
    Info,
    Hint,
}

#[derive(Debug, Clone)]
pub struct LanguageConfig {
    pub compiler: String,
    pub flags: Vec<String>,
    pub include_paths: Vec<PathBuf>,
    pub library_paths: Vec<PathBuf>,
    pub libraries: Vec<String>,
}

pub struct LanguageManager {
    languages: std::collections::HashMap<Language, Box<dyn LanguageSupport>>,
}

impl LanguageManager {
    pub fn new() -> Self {
        let mut languages = std::collections::HashMap::new();
        
        // Register language support
        languages.insert(Language::C, Box::new(c::CSupport::new()));
        languages.insert(Language::Cpp, Box::new(cpp::CppSupport::new()));
        languages.insert(Language::ObjectiveC, Box::new(objc::ObjCSupport::new()));
        languages.insert(Language::ReoX, Box::new(reox::ReoxSupport::new()));
        languages.insert(Language::Rust, Box::new(rust::RustSupport::new()));
        languages.insert(Language::Perl, Box::new(perl::PerlSupport::new()));
        
        Self { languages }
    }
    
    pub fn get_support(&self, language: &Language) -> Option<&dyn LanguageSupport> {
        self.languages.get(language).map(|support| support.as_ref())
    }
    
    pub fn compile_file(&self, file_path: &PathBuf, config: &LanguageConfig) -> Result<()> {
        let extension = file_path.extension()
            .and_then(|ext| ext.to_str())
            .ok_or_else(|| anyhow::anyhow!("No file extension found"))?;
        
        let language = Language::from_extension(extension)
            .ok_or_else(|| anyhow::anyhow!("Unsupported file extension: {}", extension))?;
        
        let support = self.get_support(&language)
            .ok_or_else(|| anyhow::anyhow!("No support for language: {:?}", language))?;
        
        let output_file = self.get_output_path(file_path, &language)?;
        support.compile(file_path, &output_file, config)
    }
    
    pub fn run_file(&self, file_path: &PathBuf, args: &[String]) -> Result<()> {
        let extension = file_path.extension()
            .and_then(|ext| ext.to_str())
            .ok_or_else(|| anyhow::anyhow!("No file extension found"))?;
        
        let language = Language::from_extension(extension)
            .ok_or_else(|| anyhow::anyhow!("Unsupported file extension: {}", extension))?;
        
        let support = self.get_support(&language)
            .ok_or_else(|| anyhow::anyhow!("No support for language: {:?}", language))?;
        
        support.run(file_path, args)
    }
    
    fn get_output_path(&self, source_file: &PathBuf, language: &Language) -> Result<PathBuf> {
        let stem = source_file.file_stem()
            .and_then(|s| s.to_str())
            .ok_or_else(|| anyhow::anyhow!("Invalid file name"))?;
        
        let output_name = match language {
            Language::C | Language::Cpp | Language::ObjectiveC => format!("{}.exe", stem),
            Language::ReoX => format!("{}.reoxexe", stem),
            Language::Rust => stem.to_string(), // Cargo handles this
            Language::Perl => stem.to_string(), // Interpreted
        };
        
        let mut output_path = source_file.parent()
            .unwrap_or_else(|| PathBuf::new().as_path())
            .to_path_buf();
        output_path.push(output_name);
        
        Ok(output_path)
    }
} 