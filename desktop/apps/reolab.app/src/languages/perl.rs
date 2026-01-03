use std::path::PathBuf;
use std::process::Command;
use anyhow::Result;
use super::{LanguageSupport, SyntaxHighlighter, Token, TokenType, LintMessage, LanguageConfig};

pub struct PerlSupport {
    highlighter: PerlSyntaxHighlighter,
}

impl PerlSupport {
    pub fn new() -> Self {
        Self {
            highlighter: PerlSyntaxHighlighter::new(),
        }
    }
}

impl LanguageSupport for PerlSupport {
    fn compile(&self, source_file: &PathBuf, output_file: &PathBuf, config: &LanguageConfig) -> Result<()> {
        // Perl is interpreted, so we just check syntax
        let mut cmd = Command::new(&config.compiler);
        
        cmd.arg("-c").arg(source_file);
        
        // Add interpreter flags
        for flag in &config.flags {
            cmd.arg(flag);
        }
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Perl syntax check failed: {}", error));
        }
        
        Ok(())
    }
    
    fn run(&self, executable: &PathBuf, args: &[String]) -> Result<()> {
        let mut cmd = Command::new(&executable);
        cmd.args(args);
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Perl execution failed: {}", error));
        }
        
        Ok(())
    }
    
    fn format(&self, source_file: &PathBuf) -> Result<()> {
        // Use perltidy if available
        let mut cmd = Command::new("perltidy");
        cmd.arg("-b").arg(source_file);
        
        let output = cmd.output()?;
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Perl formatting failed: {}", error));
        }
        
        Ok(())
    }
    
    fn lint(&self, source_file: &PathBuf) -> Result<Vec<LintMessage>> {
        // Use perl -w for warnings
        let mut cmd = Command::new("perl");
        cmd.arg("-w").arg("-c").arg(source_file);
        
        let output = cmd.output()?;
        
        let mut messages = Vec::new();
        
        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            
            // Parse perl error messages
            for line in error.lines() {
                if let Some(message) = self.parse_perl_message(line, source_file) {
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

impl PerlSupport {
    fn parse_perl_message(&self, line: &str, file: &PathBuf) -> Option<LintMessage> {
        // Simple perl message parsing
        if line.contains("syntax error") {
            Some(LintMessage {
                level: super::LintLevel::Error,
                message: line.to_string(),
                line: 1,
                column: 1,
                file: file.clone(),
            })
        } else if line.contains("warning") {
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
pub struct PerlSyntaxHighlighter {
    keywords: std::collections::HashSet<String>,
}

impl PerlSyntaxHighlighter {
    pub fn new() -> Self {
        let mut keywords = std::collections::HashSet::new();
        
        // Perl keywords
        let perl_keywords = vec![
            "my", "our", "local", "use", "require", "package", "sub", "return", "if", "else",
            "elsif", "unless", "while", "until", "for", "foreach", "do", "next", "last", "redo",
            "goto", "die", "warn", "exit", "defined", "undef", "bless", "ref", "scalar", "array",
            "hash", "keys", "values", "each", "exists", "delete", "shift", "unshift", "push",
            "pop", "splice", "sort", "reverse", "map", "grep", "split", "join", "chomp", "chop",
            "length", "substr", "index", "rindex", "uc", "lc", "ucfirst", "lcfirst", "quotemeta",
            "sprintf", "printf", "open", "close", "read", "write", "seek", "tell", "eof", "truncate",
            "flock", "select", "sysopen", "sysread", "syswrite", "sysseek", "systell", "sysclose"
        ];
        
        for keyword in perl_keywords {
            keywords.insert(keyword.to_string());
        }
        
        Self { keywords }
    }
}

impl SyntaxHighlighter for PerlSyntaxHighlighter {
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
                    if ch == '"' || ch == '\'' {
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
                    '"' | '\'' => {
                        in_string = true;
                        word_start = pos;
                    }
                    '#' => {
                        // Single line comment
                        tokens.push(Token {
                            token_type: TokenType::Comment,
                            start: pos,
                            end: current_pos + line.len(),
                            text: line[char_pos..].to_string(),
                        });
                        break;
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