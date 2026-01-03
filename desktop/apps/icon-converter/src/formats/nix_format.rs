// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use std::io::{Read, Write, Seek};
use anyhow::Result;
use image::{DynamicImage, RgbaImage};

/// Magic header for .nix files
pub const NIX_MAGIC: &[u8; 8] = b"NIXICON\x01";

/// .nix icon format version
pub const NIX_VERSION: u8 = 1;

/// Color depth options
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ColorDepth {
    RGB24 = 24,
    RGBA32 = 32,
    RGB16 = 16,
    RGB8 = 8,
}

/// .nix icon header
#[derive(Debug, Clone)]
pub struct NixIconHeader {
    pub width: u32,
    pub height: u32,
    pub color_depth: ColorDepth,
    pub frame_count: u32,
    pub animation_delay: u32, // milliseconds
    pub compression: bool,
}

/// .nix icon data
#[derive(Debug, Clone)]
pub struct NixIcon {
    pub header: NixIconHeader,
    pub frames: Vec<DynamicImage>,
}

impl NixIcon {
    /// Create a new .nix icon
    pub fn new(width: u32, height: u32, color_depth: ColorDepth) -> Self {
        Self {
            header: NixIconHeader {
                width,
                height,
                color_depth,
                frame_count: 1,
                animation_delay: 0,
                compression: false,
            },
            frames: Vec::new(),
        }
    }

    /// Add a frame to the icon
    pub fn add_frame(&mut self, frame: DynamicImage) -> Result<()> {
        if frame.width() != self.header.width || frame.height() != self.header.height {
            return Err(anyhow::anyhow!("Frame dimensions don't match icon dimensions"));
        }
        
        self.frames.push(frame);
        self.header.frame_count = self.frames.len() as u32;
        Ok(())
    }

    /// Set animation delay
    pub fn set_animation_delay(&mut self, delay_ms: u32) {
        self.header.animation_delay = delay_ms;
    }

    /// Enable/disable compression
    pub fn set_compression(&mut self, enabled: bool) {
        self.header.compression = enabled;
    }

    /// Write .nix icon to a writer
    pub fn write<W: Write + Seek>(&self, writer: &mut W) -> Result<()> {
        // Write magic header
        writer.write_all(NIX_MAGIC)?;
        
        // Write version
        writer.write_all(&[NIX_VERSION])?;
        
        // Write header
        self.write_header(writer)?;
        
        // Write frames
        for frame in &self.frames {
            self.write_frame(writer, frame)?;
        }
        
        Ok(())
    }

    /// Read .nix icon from a reader
    pub fn read<R: Read + Seek>(reader: &mut R) -> Result<Self> {
        // Read and verify magic header
        let mut magic = [0u8; 8];
        reader.read_exact(&mut magic)?;
        
        if magic != *NIX_MAGIC {
            return Err(anyhow::anyhow!("Invalid .nix file magic header"));
        }
        
        // Read version
        let mut version = [0u8; 1];
        reader.read_exact(&mut version)?;
        
        if version[0] != NIX_VERSION {
            return Err(anyhow::anyhow!("Unsupported .nix file version: {}", version[0]));
        }
        
        // Read header
        let header = Self::read_header(reader)?;
        
        // Read frames
        let mut frames = Vec::new();
        for _ in 0..header.frame_count {
            let frame = Self::read_frame(reader, &header)?;
            frames.push(frame);
        }
        
        Ok(Self { header, frames })
    }

    /// Write header to writer
    fn write_header<W: Write>(&self, writer: &mut W) -> Result<()> {
        // Write dimensions
        writer.write_all(&self.header.width.to_le_bytes())?;
        writer.write_all(&self.header.height.to_le_bytes())?;
        
        // Write color depth
        writer.write_all(&(self.header.color_depth as u8).to_le_bytes())?;
        
        // Write frame count
        writer.write_all(&self.header.frame_count.to_le_bytes())?;
        
        // Write animation delay
        writer.write_all(&self.header.animation_delay.to_le_bytes())?;
        
        // Write compression flag
        writer.write_all(&[self.header.compression as u8])?;
        
        Ok(())
    }

    /// Read header from reader
    fn read_header<R: Read>(reader: &mut R) -> Result<NixIconHeader> {
        // Read dimensions
        let mut width_bytes = [0u8; 4];
        reader.read_exact(&mut width_bytes)?;
        let width = u32::from_le_bytes(width_bytes);
        
        let mut height_bytes = [0u8; 4];
        reader.read_exact(&mut height_bytes)?;
        let height = u32::from_le_bytes(height_bytes);
        
        // Read color depth
        let mut depth_byte = [0u8; 1];
        reader.read_exact(&mut depth_byte)?;
        let color_depth = match depth_byte[0] {
            8 => ColorDepth::RGB8,
            16 => ColorDepth::RGB16,
            24 => ColorDepth::RGB24,
            32 => ColorDepth::RGBA32,
            _ => return Err(anyhow::anyhow!("Unsupported color depth: {}", depth_byte[0])),
        };
        
        // Read frame count
        let mut frame_count_bytes = [0u8; 4];
        reader.read_exact(&mut frame_count_bytes)?;
        let frame_count = u32::from_le_bytes(frame_count_bytes);
        
        // Read animation delay
        let mut delay_bytes = [0u8; 4];
        reader.read_exact(&mut delay_bytes)?;
        let animation_delay = u32::from_le_bytes(delay_bytes);
        
        // Read compression flag
        let mut compression_byte = [0u8; 1];
        reader.read_exact(&mut compression_byte)?;
        let compression = compression_byte[0] != 0;
        
        Ok(NixIconHeader {
            width,
            height,
            color_depth,
            frame_count,
            animation_delay,
            compression,
        })
    }

    /// Write frame to writer
    fn write_frame<W: Write>(&self, writer: &mut W, frame: &DynamicImage) -> Result<()> {
        let rgba_image = frame.to_rgba8();
        let pixels = rgba_image.as_raw();
        
        if self.header.compression {
            // TODO: Implement compression
            writer.write_all(pixels)?;
        } else {
            writer.write_all(pixels)?;
        }
        
        Ok(())
    }

    /// Read frame from reader
    fn read_frame<R: Read>(reader: &mut R, header: &NixIconHeader) -> Result<DynamicImage> {
        let pixel_count = (header.width * header.height) as usize;
        let bytes_per_pixel = match header.color_depth {
            ColorDepth::RGB8 => 1,
            ColorDepth::RGB16 => 2,
            ColorDepth::RGB24 => 3,
            ColorDepth::RGBA32 => 4,
        };
        
        let mut buffer = vec![0u8; pixel_count * bytes_per_pixel];
        reader.read_exact(&mut buffer)?;
        
        if header.compression {
            // TODO: Implement decompression
        }
        
        // Convert to RGBA image
        let mut rgba_buffer = Vec::new();
        for i in 0..pixel_count {
            let pixel_start = i * bytes_per_pixel;
            
            match header.color_depth {
                ColorDepth::RGB8 => {
                    let gray = buffer[pixel_start];
                    rgba_buffer.extend_from_slice(&[gray, gray, gray, 255]);
                }
                ColorDepth::RGB16 => {
                    let r = buffer[pixel_start];
                    let g = buffer[pixel_start + 1];
                    rgba_buffer.extend_from_slice(&[r, g, 0, 255]);
                }
                ColorDepth::RGB24 => {
                    let r = buffer[pixel_start];
                    let g = buffer[pixel_start + 1];
                    let b = buffer[pixel_start + 2];
                    rgba_buffer.extend_from_slice(&[r, g, b, 255]);
                }
                ColorDepth::RGBA32 => {
                    rgba_buffer.extend_from_slice(&buffer[pixel_start..pixel_start + 4]);
                }
            }
        }
        
        let image = RgbaImage::from_raw(
            header.width,
            header.height,
            rgba_buffer,
        ).ok_or_else(|| anyhow::anyhow!("Failed to create image from buffer"))?;
        
        Ok(DynamicImage::ImageRgba8(image))
    }

    /// Get the first frame as a DynamicImage
    pub fn get_frame(&self, index: usize) -> Option<&DynamicImage> {
        self.frames.get(index)
    }

    /// Get all frames
    pub fn get_frames(&self) -> &[DynamicImage] {
        &self.frames
    }

    /// Check if the icon is animated
    pub fn is_animated(&self) -> bool {
        self.header.frame_count > 1
    }

    /// Get animation delay in milliseconds
    pub fn get_animation_delay(&self) -> u32 {
        self.header.animation_delay
    }
} 