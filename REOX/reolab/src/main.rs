// REOX Compiler - Main Entry Point
// Zero external dependencies

mod lexer;
mod parser;
mod typechecker;
mod codegen;
mod cli;

use std::env;
use std::process;

fn main() {
    let args = cli::parse_args();

    match args {
        Ok(args) => {
            if let Err(e) = compile(&args) {
                eprintln!("{}", e);
                process::exit(1);
            }
        }
        Err(e) => {
            eprintln!("error: {}", e);
            eprintln!();
            cli::print_usage();
            process::exit(1);
        }
    }
}

fn compile(args: &cli::Args) -> Result<(), String> {
    // Read source file
    let source = std::fs::read_to_string(&args.input)
        .map_err(|e| format!("failed to read '{}': {}", args.input, e))?;

    // Lexical analysis
    let tokens = lexer::tokenize(&source)
        .map_err(|e| e.display())?;

    // Parse
    let ast = parser::parse(&tokens);

    // Type check
    let checked_ast = typechecker::check(&ast);

    // Generate code
    let output_path = args.output.clone()
        .unwrap_or_else(|| {
            let stem = std::path::Path::new(&args.input)
                .file_stem()
                .and_then(|s| s.to_str())
                .unwrap_or("output");
            format!("{}.c", stem)
        });

    codegen::generate(&checked_ast, &output_path)
        .map_err(|e| format!("code generation failed: {}", e))?;

    println!("compiled: {} -> {}", args.input, output_path);

    Ok(())
}
