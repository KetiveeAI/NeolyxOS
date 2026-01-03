// REOX Compiler - CLI Argument Parser
// Self-built, zero external dependencies
// Supports full compilation pipeline with LTO

use std::env;
use std::process::Command;
use std::path::Path;

/// Compiler arguments
pub struct Args {
    pub input: String,
    pub output: Option<String>,
    pub emit: EmitType,
    pub opt_level: OptLevel,
    pub lto: bool,
    pub strip: bool,
    pub verbose: bool,
    pub runtime_path: Option<String>,
}

/// Output type
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EmitType {
    C,      // Generate C code only
    Obj,    // Compile to object file
    Exe,    // Compile to executable (default)
}

impl Default for EmitType {
    fn default() -> Self {
        EmitType::C
    }
}

/// Optimization level
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OptLevel {
    O0,  // No optimization
    O1,  // Basic optimization
    O2,  // Standard optimization
    O3,  // Aggressive optimization
    Os,  // Size optimization
}

impl OptLevel {
    pub fn to_flag(&self) -> &'static str {
        match self {
            OptLevel::O0 => "-O0",
            OptLevel::O1 => "-O1",
            OptLevel::O2 => "-O2",
            OptLevel::O3 => "-O3",
            OptLevel::Os => "-Os",
        }
    }
}

impl Default for OptLevel {
    fn default() -> Self {
        OptLevel::O2
    }
}

/// Parse command line arguments
pub fn parse_args() -> Result<Args, String> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        return Err("no input file specified".to_string());
    }

    let mut input: Option<String> = None;
    let mut output: Option<String> = None;
    let mut emit = EmitType::C;
    let mut opt_level = OptLevel::O2;
    let mut lto = false;
    let mut strip = false;
    let mut verbose = false;
    let mut runtime_path: Option<String> = None;

    let mut i = 1;
    while i < args.len() {
        let arg = &args[i];

        match arg.as_str() {
            "-o" | "--output" => {
                i += 1;
                if i >= args.len() {
                    return Err("expected output file after -o".to_string());
                }
                output = Some(args[i].clone());
            }
            "--emit" => {
                i += 1;
                if i >= args.len() {
                    return Err("expected emit type after --emit".to_string());
                }
                emit = match args[i].as_str() {
                    "c" => EmitType::C,
                    "obj" => EmitType::Obj,
                    "exe" => EmitType::Exe,
                    other => return Err(format!("unknown emit type: {}", other)),
                };
            }
            "-O0" => opt_level = OptLevel::O0,
            "-O1" => opt_level = OptLevel::O1,
            "-O2" => opt_level = OptLevel::O2,
            "-O3" => opt_level = OptLevel::O3,
            "-Os" => opt_level = OptLevel::Os,
            "--lto" | "-flto" => lto = true,
            "--strip" | "-s" => strip = true,
            "--runtime" => {
                i += 1;
                if i >= args.len() {
                    return Err("expected runtime path after --runtime".to_string());
                }
                runtime_path = Some(args[i].clone());
            }
            "-v" | "--verbose" => verbose = true,
            "-h" | "--help" => {
                print_usage();
                std::process::exit(0);
            }
            "-V" | "--version" => {
                println!("reoxc 0.1.0");
                std::process::exit(0);
            }
            _ => {
                if arg.starts_with('-') {
                    return Err(format!("unknown option: {}", arg));
                }
                if input.is_some() {
                    return Err("multiple input files not supported".to_string());
                }
                input = Some(arg.clone());
            }
        }

        i += 1;
    }

    let input = input.ok_or("no input file specified")?;

    Ok(Args {
        input,
        output,
        emit,
        opt_level,
        lto,
        strip,
        verbose,
        runtime_path,
    })
}

/// Print usage information
pub fn print_usage() {
    println!("reoxc - REOX Language Compiler for NeolyxOS");
    println!();
    println!("USAGE:");
    println!("    reoxc [OPTIONS] <INPUT>");
    println!();
    println!("OPTIONS:");
    println!("    -o, --output <FILE>    Output file path");
    println!("    --emit <TYPE>          Output type: c, obj, exe (default: c)");
    println!();
    println!("  Optimization:");
    println!("    -O0                    No optimization");
    println!("    -O1                    Basic optimization");
    println!("    -O2                    Standard optimization (default)");
    println!("    -O3                    Aggressive optimization");
    println!("    -Os                    Optimize for size");
    println!("    --lto                  Enable Link-Time Optimization");
    println!("    --strip                Strip symbols from output");
    println!();
    println!("  Other:");
    println!("    --runtime <PATH>       Path to runtime library");
    println!("    -v, --verbose          Verbose output");
    println!("    -h, --help             Print help information");
    println!("    -V, --version          Print version information");
    println!();
    println!("EXAMPLES:");
    println!("    reoxc main.rx -o main.c              Generate C code");
    println!("    reoxc main.rx --emit exe -o app     Compile to executable");
    println!("    reoxc main.rx --emit exe -O3 --lto  Full optimization");
}

/// Compile C to executable
pub fn compile_c_to_exe(
    c_file: &str,
    output: &str,
    args: &Args,
) -> Result<(), String> {
    let mut cmd = Command::new("gcc");
    
    // Add optimization flags
    cmd.arg(args.opt_level.to_flag());
    
    if args.lto {
        cmd.arg("-flto");
    }
    
    // Add runtime include path
    if let Some(ref runtime) = args.runtime_path {
        cmd.arg("-I").arg(runtime);
        cmd.arg("-L").arg(runtime);
    }
    
    // Input and output
    cmd.arg("-o").arg(output);
    cmd.arg(c_file);
    
    // Link runtime and math
    if let Some(ref runtime) = args.runtime_path {
        cmd.arg(format!("{}/libreox_runtime.a", runtime));
    }
    cmd.arg("-lm");
    
    // Section garbage collection
    cmd.arg("-Wl,--gc-sections");
    
    if args.verbose {
        println!("[gcc] {:?}", cmd);
    }
    
    let status = cmd.status()
        .map_err(|e| format!("failed to run gcc: {}", e))?;
    
    if !status.success() {
        return Err("gcc compilation failed".to_string());
    }
    
    // Strip if requested
    if args.strip {
        let status = Command::new("strip")
            .arg(output)
            .status()
            .map_err(|e| format!("failed to run strip: {}", e))?;
        
        if !status.success() {
            eprintln!("warning: strip failed");
        }
    }
    
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_emit_type_default() {
        assert_eq!(EmitType::default(), EmitType::C);
    }

    #[test]
    fn test_opt_level_flags() {
        assert_eq!(OptLevel::O3.to_flag(), "-O3");
        assert_eq!(OptLevel::Os.to_flag(), "-Os");
    }
}
