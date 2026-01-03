use std::path::PathBuf;
use std::process::Command;
use anyhow::Result;
use super::{LanguageSupport, SyntaxHighlighter, Token, TokenType, LintMessage, LanguageConfig};

pub struct ReoxSupport {
    highlighter: ReoxSyntaxHighlighter,
}

impl ReoxSupport {
    pub fn new() -> Self {
        Self {
            highlighter: ReoxSyntaxHighlighter::new(),
        }
    }
}

impl LanguageSupport for ReoxSupport {
    fn compile(&self, source_file: &PathBuf, output_file: &PathBuf, config: &LanguageConfig) -> Result<()> {
        // Use our custom reoxc compiler
        let mut cmd = Command::new(&config.compiler);
        
        // Add source file
        cmd.arg(source_file);
        
        // Add output file
        cmd.arg("-o").arg(output_file);
        
        // Add compiler flags
        for flag in &config.flags {
            cmd.arg(flag);
        }
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("ReoX compilation failed: {}", error));
        }
        
        Ok(())
    }
    
    fn run(&self, executable: &PathBuf, args: &[String]) -> Result<()> {
        let mut cmd = Command::new(executable);
        cmd.args(args);
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("ReoX execution failed: {}", error));
        }
        
        Ok(())
    }
    
    fn format(&self, source_file: &PathBuf) -> Result<()> {
        // TODO: Implement ReoX formatter
        Ok(())
    }
    
    fn lint(&self, source_file: &PathBuf) -> Result<Vec<LintMessage>> {
        // TODO: Implement ReoX linter
        Ok(Vec::new())
    }
    
    fn get_syntax_highlighter(&self) -> Box<dyn SyntaxHighlighter> {
        Box::new(self.highlighter.clone())
    }
}

#[derive(Clone)]
pub struct ReoxSyntaxHighlighter {
    keywords: std::collections::HashSet<String>,
}

impl ReoxSyntaxHighlighter {
    pub fn new() -> Self {
        let mut keywords = std::collections::HashSet::new();
        
        // ReoX keywords
        let reox_keywords = vec![
            "let", "fn", "if", "else", "while", "for", "return", "import", "struct", "enum",
            "match", "try", "catch", "async", "await", "spawn", "channel", "int", "float",
            "bool", "string", "char", "array", "map", "any", "Result", "Error", "kernel",
            "syscall", "hardware", "true", "false"
        ];
        
        for keyword in reox_keywords {
            keywords.insert(keyword.to_string());
        }
        
        Self { keywords }
    }
}

impl SyntaxHighlighter for ReoxSyntaxHighlighter {
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