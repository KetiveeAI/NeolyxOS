// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

pub mod converter;
pub mod formats;
pub mod utils;

pub use converter::{IconConverter, ConversionOptions};
pub use formats::nix_format::NixIcon;

/// Quality levels for conversion
#[derive(Debug, Clone, Copy)]
pub enum Quality {
    Low,
    Medium,
    High,
}

impl Quality {
    /// Get JPEG quality value
    pub fn jpeg_quality(&self) -> u8 {
        match self {
            Quality::Low => 60,
            Quality::Medium => 80,
            Quality::High => 95,
        }
    }

    /// Get PNG compression level
    pub fn png_compression(&self) -> u8 {
        match self {
            Quality::Low => 1,
            Quality::Medium => 6,
            Quality::High => 9,
        }
    }
} 