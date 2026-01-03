use std::path::PathBuf;
use std::process::Command;
use anyhow::Result;
use super::{LanguageSupport, SyntaxHighlighter, Token, TokenType, LintMessage, LanguageConfig};

pub struct CSupport {
    highlighter: CSyntaxHighlighter,
}

impl CSupport {
    pub fn new() -> Self {
        Self {
            highlighter: CSyntaxHighlighter::new(),
        }
    }
}

impl LanguageSupport for CSupport {
    fn compile(&self, source_file: &PathBuf, output_file: &PathBuf, config: &LanguageConfig) -> Result<()> {
        let mut cmd = Command::new(&config.compiler);
        
        // Add source file
        cmd.arg(source_file);
        
        // Add output file
        cmd.arg("-o").arg(output_file);
        
        // Add compiler flags
        for flag in &config.flags {
            cmd.arg(flag);
        }
        
        // Add include paths
        for include_path in &config.include_paths {
            cmd.arg("-I").arg(include_path);
        }
        
        // Add library paths
        for lib_path in &config.library_paths {
            cmd.arg("-L").arg(lib_path);
        }
        
        // Add libraries
        for library in &config.libraries {
            cmd.arg(format!("-l{}", library));
        }
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Compilation failed: {}", error));
        }
        
        Ok(())
    }
    
    fn run(&self, executable: &PathBuf, args: &[String]) -> Result<()> {
        let mut cmd = Command::new(executable);
        cmd.args(args);
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Execution failed: {}", error));
        }
        
        Ok(())
    }
    
    fn format(&self, source_file: &PathBuf) -> Result<()> {
        // Use clang-format if available
        let mut cmd = Command::new("clang-format");
        cmd.arg("-i").arg(source_file);
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Formatting failed: {}", error));
        }
        
        Ok(())
    }
    
    fn lint(&self, source_file: &PathBuf) -> Result<Vec<LintMessage>> {
        let mut messages = Vec::new();
        
        // Use gcc with warnings enabled for linting
        let mut cmd = Command::new("gcc");
        cmd.arg("-fsyntax-only")
            .arg("-Wall")
            .arg("-Wextra")
            .arg("-Werror")
            .arg(source_file);
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            
            // Parse gcc error messages
            for line in error.lines() {
                if let Some(message) = self.parse_gcc_error(line, source_file) {
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

impl CSupport {
    fn parse_gcc_error(&self, line: &str, file: &PathBuf) -> Option<LintMessage> {
        // Simple GCC error parsing
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
pub struct CSyntaxHighlighter {
    keywords: std::collections::HashSet<String>,
}

impl CSyntaxHighlighter {
    pub fn new() -> Self {
        let mut keywords = std::collections::HashSet::new();
        
        // C keywords
        let c_keywords = vec![
            "auto", "break", "case", "char", "const", "continue", "default", "do",
            "double", "else", "enum", "extern", "float", "for", "goto", "if",
            "int", "long", "register", "return", "short", "signed", "sizeof", "static",
            "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while"
        ];
        
        for keyword in c_keywords {
            keywords.insert(keyword.to_string());
        }
        
        Self { keywords }
    }
}

impl SyntaxHighlighter for CSyntaxHighlighter {
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