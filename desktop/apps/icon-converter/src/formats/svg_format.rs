// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use anyhow::Result;
use std::path::Path;
use image::DynamicImage;
use resvg::{render, usvg};

/// Load SVG file and convert to image
pub fn load_svg(path: &Path) -> Result<DynamicImage> {
    // Read SVG file
    let svg_data = std::fs::read(path)?;
    let svg_str = String::from_utf8(svg_data)?;
    
    // Parse SVG
    let opt = usvg::Options::default();
    let tree = usvg::Tree::from_str(&svg_str, &opt)?;
    
    // Get SVG dimensions
    let size = tree.size;
    let width = size.width() as u32;
    let height = size.height() as u32;
    
    // Create image buffer
    let mut pixmap = tiny_skia::Pixmap::new(width, height)
        .ok_or_else(|| anyhow::anyhow!("Failed to create pixmap"))?;
    
    // Render SVG to pixmap
    render(&tree, usvg::FitTo::Original, pixmap.as_mut());
    
    // Convert pixmap to DynamicImage
    let rgba_data = pixmap.data();
    let image = image::RgbaImage::from_raw(width, height, rgba_data.to_vec())
        .ok_or_else(|| anyhow::anyhow!("Failed to create image from pixmap"))?;
    
    Ok(DynamicImage::ImageRgba8(image))
}

/// Convert SVG to specific size
pub fn load_svg_with_size(path: &Path, width: u32, height: u32) -> Result<DynamicImage> {
    // Read SVG file
    let svg_data = std::fs::read(path)?;
    let svg_str = String::from_utf8(svg_data)?;
    
    // Parse SVG
    let opt = usvg::Options::default();
    let tree = usvg::Tree::from_str(&svg_str, &opt)?;
    
    // Create image buffer with target size
    let mut pixmap = tiny_skia::Pixmap::new(width, height)
        .ok_or_else(|| anyhow::anyhow!("Failed to create pixmap"))?;
    
    // Render SVG to pixmap with scaling
    render(&tree, usvg::FitTo::Size(width, height), pixmap.as_mut());
    
    // Convert pixmap to DynamicImage
    let rgba_data = pixmap.data();
    let image = image::RgbaImage::from_raw(width, height, rgba_data.to_vec())
        .ok_or_else(|| anyhow::anyhow!("Failed to create image from pixmap"))?;
    
    Ok(DynamicImage::ImageRgba8(image))
} 