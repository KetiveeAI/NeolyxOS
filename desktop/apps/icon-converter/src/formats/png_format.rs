// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use anyhow::Result;
use std::path::Path;
use image::DynamicImage;

/// Load PNG file
pub fn load_png(path: &Path) -> Result<DynamicImage> {
    let image = image::open(path)?;
    Ok(image)
}

/// Save PNG file with quality settings
pub fn save_png(image: &DynamicImage, path: &Path, compression: u8) -> Result<()> {
    let rgba_image = image.to_rgba8();
    let mut file = std::fs::File::create(path)?;
    
    let mut encoder = png::Encoder::new(&mut file, rgba_image.width(), rgba_image.height());
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    encoder.set_compression(png::Compression::Best);
    
    let mut writer = encoder.write_header()?;
    writer.write_image_data(rgba_image.as_raw())?;
    
    Ok(())
} 