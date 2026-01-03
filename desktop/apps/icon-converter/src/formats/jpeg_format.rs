// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use anyhow::Result;
use std::path::Path;
use image::DynamicImage;

/// Load JPEG file
pub fn load_jpeg(path: &Path) -> Result<DynamicImage> {
    let image = image::open(path)?;
    Ok(image)
}

/// Save JPEG file with quality settings
pub fn save_jpeg(image: &DynamicImage, path: &Path, quality: u8) -> Result<()> {
    let rgb_image = image.to_rgb8();
    let mut file = std::fs::File::create(path)?;
    
    let mut encoder = image::codecs::jpeg::JpegEncoder::new_with_quality(&mut file, quality);
    encoder.encode(rgb_image.as_raw(), rgb_image.width(), rgb_image.height(), image::ColorType::Rgb8)?;
    
    Ok(())
} 