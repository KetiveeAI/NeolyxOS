// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use clap::Parser;
use anyhow::Result;
use icon_converter::{IconConverter, ConversionOptions};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "icon-converter")]
#[command(about = "Convert images to Neolyx .nix icon format")]
#[command(version)]
struct Cli {
    /// Input file or directory
    #[arg(value_name = "INPUT")]
    input: PathBuf,

    /// Output file or directory
    #[arg(value_name = "OUTPUT")]
    output: PathBuf,

    /// Target size (e.g., 64x64)
    #[arg(short, long)]
    size: Option<String>,

    /// Quality level (low, medium, high)
    #[arg(short, long, default_value = "high")]
    quality: String,

    /// Enable compression
    #[arg(short, long)]
    compress: bool,

    /// Animation delay in milliseconds
    #[arg(long)]
    animation_delay: Option<u32>,

    /// Batch mode for directories
    #[arg(short, long)]
    batch: bool,

    /// Output directory for batch mode
    #[arg(long)]
    output_dir: Option<PathBuf>,

    /// Multiple sizes for batch mode (comma-separated)
    #[arg(long)]
    sizes: Option<String>,

    /// Preview mode (don't write files)
    #[arg(short, long)]
    preview: bool,

    /// Verbose output
    #[arg(short, long)]
    verbose: bool,
}

fn main() -> Result<()> {
    // Parse command line arguments
    let cli = Cli::parse();

    // Initialize logging
    if cli.verbose {
        env_logger::init();
    }

    // Create conversion options
    let options = ConversionOptions {
        size: parse_size(&cli.size)?,
        quality: parse_quality(&cli.quality)?,
        compression: cli.compress,
        animation_delay: cli.animation_delay,
        preview: cli.preview,
    };

    // Create converter
    let converter = IconConverter::new(options)?;

    if cli.batch {
        // Batch conversion mode
        let output_dir = cli.output_dir.unwrap_or_else(|| PathBuf::from("."));
        let sizes = parse_sizes(&cli.sizes)?;
        
        converter.convert_batch(&cli.input, &output_dir, &sizes)?;
    } else {
        // Single file conversion
        converter.convert_file(&cli.input, &cli.output)?;
    }

    Ok(())
}

fn parse_size(size_str: &Option<String>) -> Result<Option<(u32, u32)>> {
    if let Some(size) = size_str {
        let parts: Vec<&str> = size.split('x').collect();
        if parts.len() == 2 {
            let width = parts[0].parse::<u32>()?;
            let height = parts[1].parse::<u32>()?;
            Ok(Some((width, height)))
        } else {
            Err(anyhow::anyhow!("Invalid size format. Use WIDTHxHEIGHT (e.g., 64x64)"))
        }
    } else {
        Ok(None)
    }
}

fn parse_quality(quality: &str) -> Result<icon_converter::Quality> {
    match quality.to_lowercase().as_str() {
        "low" => Ok(icon_converter::Quality::Low),
        "medium" => Ok(icon_converter::Quality::Medium),
        "high" => Ok(icon_converter::Quality::High),
        _ => Err(anyhow::anyhow!("Invalid quality level. Use: low, medium, high")),
    }
}

fn parse_sizes(sizes_str: &Option<String>) -> Result<Vec<(u32, u32)>> {
    if let Some(sizes) = sizes_str {
        let mut result = Vec::new();
        for size_str in sizes.split(',') {
            if let Some(size) = parse_size(&Some(size_str.to_string()))? {
                result.push(size);
            }
        }
        Ok(result)
    } else {
        // Default sizes
        Ok(vec![(16, 16), (32, 32), (64, 64), (128, 128)])
    }
} 