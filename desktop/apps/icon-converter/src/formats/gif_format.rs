// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use anyhow::Result;
use std::path::Path;
use image::DynamicImage;

/// Load GIF file (single frame or animated)
pub fn load_gif(path: &Path) -> Result<DynamicImage> {
    let image = image::open(path)?;
    Ok(image)
}

/// Load all frames from animated GIF
pub fn load_gif_frames(path: &Path) -> Result<Vec<DynamicImage>> {
    let mut frames = Vec::new();
    let mut decoder = gif::DecodeOptions::new();
    decoder.set_color_output(gif::ColorOutput::RGBA);
    
    let mut file = std::fs::File::open(path)?;
    let mut decoder = decoder.read_info(&mut file)?;
    
    while let Some(frame) = decoder.read_next_frame()? {
        let image = image::RgbaImage::from_raw(
            frame.width as u32,
            frame.height as u32,
            frame.buffer.to_vec(),
        ).ok_or_else(|| anyhow::anyhow!("Failed to create image from GIF frame"))?;
        
        frames.push(DynamicImage::ImageRgba8(image));
    }
    
    Ok(frames)
}

/// Get GIF animation delay
pub fn get_gif_delay(path: &Path) -> Result<u32> {
    let mut file = std::fs::File::open(path)?;
    let mut decoder = gif::DecodeOptions::new();
    let mut decoder = decoder.read_info(&mut file)?;
    
    if let Some(frame) = decoder.read_next_frame()? {
        Ok(frame.delay as u32 * 10) // Convert to milliseconds
    } else {
        Ok(0)
    }
} 