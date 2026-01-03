// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use anyhow::Result;
use std::path::{Path, PathBuf};
use walkdir::WalkDir;
use indicatif::{ProgressBar, ProgressStyle};
use image::DynamicImage;

use crate::{Quality, formats::nix_format::NixIcon};
use crate::formats::{svg_format, png_format, jpeg_format, gif_format};

/// Conversion options
#[derive(Debug, Clone)]
pub struct ConversionOptions {
    pub size: Option<(u32, u32)>,
    pub quality: Quality,
    pub compression: bool,
    pub animation_delay: Option<u32>,
    pub preview: bool,
}

/// Main icon converter
pub struct IconConverter {
    options: ConversionOptions,
}

impl IconConverter {
    /// Create a new icon converter
    pub fn new(options: ConversionOptions) -> Result<Self> {
        Ok(Self { options })
    }

    /// Convert a single file
    pub fn convert_file(&self, input: &Path, output: &Path) -> Result<()> {
        log::info!("Converting {} to {}", input.display(), output.display());

        // Load input image
        let image = self.load_image(input)?;
        
        // Resize if needed
        let image = if let Some((width, height)) = self.options.size {
            self.resize_image(image, width, height)?
        } else {
            image
        };

        // Convert to .nix format
        let mut nix_icon = NixIcon::new(
            image.width(),
            image.height(),
            crate::formats::nix_format::ColorDepth::RGBA32,
        );

        // Set animation delay if specified
        if let Some(delay) = self.options.animation_delay {
            nix_icon.set_animation_delay(delay);
        }

        // Enable compression if requested
        nix_icon.set_compression(self.options.compression);

        // Add the image frame
        nix_icon.add_frame(image)?;

        if self.options.preview {
            log::info!("Preview mode - not writing file");
            log::info!("Icon size: {}x{}", nix_icon.header.width, nix_icon.header.height);
            log::info!("Frames: {}", nix_icon.header.frame_count);
            log::info!("Compression: {}", nix_icon.header.compression);
        } else {
            // Write output file
            let mut file = std::fs::File::create(output)?;
            nix_icon.write(&mut file)?;
            log::info!("Successfully converted to {}", output.display());
        }

        Ok(())
    }

    /// Convert multiple files in batch mode
    pub fn convert_batch(&self, input_dir: &Path, output_dir: &Path, sizes: &[(u32, u32)]) -> Result<()> {
        log::info!("Batch converting from {} to {}", input_dir.display(), output_dir.display());

        // Create output directory if it doesn't exist
        std::fs::create_dir_all(output_dir)?;

        // Find all image files
        let image_files: Vec<PathBuf> = WalkDir::new(input_dir)
            .into_iter()
            .filter_map(|e| e.ok())
            .filter(|e| e.file_type().is_file())
            .filter(|e| self.is_image_file(e.path()))
            .map(|e| e.path().to_path_buf())
            .collect();

        if image_files.is_empty() {
            log::warn!("No image files found in {}", input_dir.display());
            return Ok(());
        }

        // Create progress bar
        let progress = ProgressBar::new(image_files.len() as u64);
        progress.set_style(
            ProgressStyle::default_bar()
                .template("{spinner:.green} [{elapsed_precise}] [{bar:40.cyan/blue}] {pos}/{len} ({eta})")
                .unwrap()
                .progress_chars("#>-"),
        );

        for input_file in &image_files {
            progress.set_message(format!("Converting {}", input_file.file_name().unwrap().to_string_lossy()));

            // Create output filename
            let stem = input_file.file_stem().unwrap().to_string_lossy();
            let output_file = output_dir.join(format!("{}.nix", stem));

            // Convert with each size
            for &(width, height) in sizes {
                let size_suffix = format!("_{}x{}", width, height);
                let output_file = output_dir.join(format!("{}{}.nix", stem, size_suffix));

                let mut options = self.options.clone();
                options.size = Some((width, height));

                let converter = IconConverter::new(options)?;
                if let Err(e) = converter.convert_file(input_file, &output_file) {
                    log::error!("Failed to convert {}: {}", input_file.display(), e);
                }
            }

            progress.inc(1);
        }

        progress.finish_with_message("Batch conversion completed");
        Ok(())
    }

    /// Load image from file
    fn load_image(&self, path: &Path) -> Result<DynamicImage> {
        let extension = path.extension()
            .and_then(|ext| ext.to_str())
            .unwrap_or("")
            .to_lowercase();

        match extension.as_str() {
            "svg" => svg_format::load_svg(path),
            "png" => png_format::load_png(path),
            "jpg" | "jpeg" => jpeg_format::load_jpeg(path),
            "gif" => gif_format::load_gif(path),
            "bmp" => self.load_with_image_crate(path),
            "ico" => self.load_with_image_crate(path),
            "webp" => self.load_with_image_crate(path),
            _ => {
                // Try to load with image crate as fallback
                self.load_with_image_crate(path)
            }
        }
    }

    /// Load image using the image crate
    fn load_with_image_crate(&self, path: &Path) -> Result<DynamicImage> {
        let image = image::open(path)?;
        Ok(image)
    }

    /// Resize image to target dimensions
    fn resize_image(&self, image: DynamicImage, width: u32, height: u32) -> Result<DynamicImage> {
        let resized = image.resize(width, height, image::imageops::FilterType::Lanczos3);
        Ok(resized)
    }

    /// Check if file is an image
    fn is_image_file(&self, path: &Path) -> bool {
        let extension = path.extension()
            .and_then(|ext| ext.to_str())
            .unwrap_or("")
            .to_lowercase();

        matches!(extension.as_str(), 
            "svg" | "png" | "jpg" | "jpeg" | "gif" | "bmp" | "ico" | "webp"
        )
    }
} 