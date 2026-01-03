use std::path::PathBuf;
use std::process::Command;
use anyhow::Result;
use super::{LanguageSupport, SyntaxHighlighter, Token, TokenType, LintMessage, LanguageConfig};

pub struct RustSupport {
    highlighter: RustSyntaxHighlighter,
}

impl RustSupport {
    pub fn new() -> Self {
        Self {
            highlighter: RustSyntaxHighlighter::new(),
        }
    }
}

impl LanguageSupport for RustSupport {
    fn compile(&self, source_file: &PathBuf, output_file: &PathBuf, config: &LanguageConfig) -> Result<()> {
        // Use cargo for Rust compilation
        let mut cmd = Command::new(&config.compiler);
        
        cmd.arg("build");
        
        // Add compiler flags
        for flag in &config.flags {
            cmd.arg(flag);
        }
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Rust compilation failed: {}", error));
        }
        
        Ok(())
    }
    
    fn run(&self, executable: &PathBuf, args: &[String]) -> Result<()> {
        let mut cmd = Command::new(executable);
        cmd.args(args);
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Rust execution failed: {}", error));
        }
        
        Ok(())
    }
    
    fn format(&self, source_file: &PathBuf) -> Result<()> {
        // Use rustfmt
        let mut cmd = Command::new("rustfmt");
        cmd.arg(source_file);
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Rust formatting failed: {}", error));
        }
        
        Ok(())
    }
    
    fn lint(&self, source_file: &PathBuf) -> Result<Vec<LintMessage>> {
        // Use cargo clippy
        let mut cmd = Command::new("cargo");
        cmd.arg("clippy");
        
        let output = cmd.output()?;
        
        let mut messages = Vec::new();
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            
            // Parse clippy messages
            for line in error.lines() {
                if let Some(message) = self.parse_clippy_message(line, source_file) {
                    messages.push(message);
                }
            }
        }
        
        Ok(messages)
    }
    
    fn get_syntax_highlighter(&self) -> Box<dyn SyntaxHighlighter> {
        Box::new(self.highlighter.clone())
    }
}

impl RustSupport {
    fn parse_clippy_message(&self, line: &str, file: &PathBuf) -> Option<LintMessage> {
        // Simple clippy message parsing
        if line.contains("error:") {
            Some(LintMessage {
                level: super::LintLevel::Error,
                message: line.to_string(),
                line: 1,
                column: 1,
                file: file.clone(),
            })
        } else if line.contains("warning:") {
            Some(LintMessage {
                level: super::LintLevel::Warning,
                message: line.to_string(),
                line: 1,
                column: 1,
                file: file.clone(),
            })
        } else {
            None
        }
    }
}

#[derive(Clone)]
pub struct RustSyntaxHighlighter {
    keywords: std::collections::HashSet<String>,
}

impl RustSyntaxHighlighter {
    pub fn new() -> Self {
        let mut keywords = std::collections::HashSet::new();
        
        // Rust keywords
        let rust_keywords = vec![
            "as", "break", "const", "continue", "crate", "else", "enum", "extern", "false",
            "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut",
            "pub", "ref", "return", "self", "Self", "static", "struct", "super", "trait",
            "true", "type", "unsafe", "use", "where", "while", "async", "await", "dyn"
        ];
        
        for keyword in rust_keywords {
            keywords.insert(keyword.to_string());
        }
        
        Self { keywords }
    }
}

impl SyntaxHighlighter for RustSyntaxHighlighter {
    fn highlight(&self, source: &str) -> Vec<Token> {
        let mut tokens = Vec::new();
        let mut current_pos = 0;
        
        // Simple tokenization
        for (line_num, line) in source.lines().enumerate() {
            let mut word_start = 0;
            let mut in_string = false;
            let mut in_comment = false;
            
            for (char_pos, ch) in line.chars().enumerate() {
                let pos = current_pos + char_pos;
                
                if in_comment {
                    // Continue comment until end of line
                    continue;
                }
                
                if in_string {
                    if ch == '"' {
                        in_string = false;
                        tokens.push(Token {
                            token_type: TokenType::String,
                            start: word_start,
                            end: pos + 1,
                            text: line[word_start..=char_pos].to_string(),
                        });
                    }
                    continue;
                }
                
                match ch {
                    '"' => {
                        in_string = true;
                        word_start = pos;
                    }
                    '/' => {
                        if char_pos + 1 < line.len() && line.chars().nth(char_pos + 1) == Some('/') {
                            // Single line comment
                            tokens.push(Token {
                                token_type: TokenType::Comment,
                                start: pos,
                                end: current_pos + line.len(),
                                text: line[char_pos..].to_string(),
                            });
                            break;
                        }
                    }
                    ' ' | '\t' | '\n' => {
                        // End of word
                        if word_start < pos {
                            let word = &line[word_start..char_pos];
                            let token_type = if self.keywords.contains(word) {
                                TokenType::Keyword
                            } else {
                                TokenType::Identifier
                            };
                            
                            tokens.push(Token {
                                token_type,
                                start: word_start,
                                end: pos,
                                text: word.to_string(),
                            });
                        }
                        word_start = pos + 1;
                    }
                    _ => {
                        // Continue word
                    }
                }
            }
            
            current_pos += line.len() + 1; // +1 for newline
        }
        
        tokens
    }
} 