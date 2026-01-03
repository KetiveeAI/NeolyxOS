// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use anyhow::Result;
use crate::core::vm_profile::VMProfileTemplate;

/// VM Templates - predefined configurations for different use cases
pub struct VMTemplates {
    templates: Vec<VMProfileTemplate>,
}

impl VMTemplates {
    /// Create a new VM templates instance
    pub fn new() -> Result<Self> {
        let templates = Self::load_templates();
        Ok(Self { templates })
    }

    /// Get all available templates
    pub fn get_templates(&self) -> &[VMProfileTemplate] {
        &self.templates
    }

    /// Find template by name
    pub fn find_template(&self, name: &str) -> Option<&VMProfileTemplate> {
        self.templates.iter().find(|t| t.name == name)
    }

    /// Load templates from configuration
    fn load_templates() -> Vec<VMProfileTemplate> {
        // For now, return empty vector - templates are loaded in VMProfileGenerator
        Vec::new()
    }
} 