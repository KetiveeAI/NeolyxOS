// REOX Profiler - Core Module
// Zero-overhead profiling for REOX programs
// Designed for NeolyxOS native integration

mod instrumentation;
mod reporter;

pub use instrumentation::*;
pub use reporter::*;

use std::collections::HashMap;
use std::time::{Duration, Instant};

/// Profiler configuration
#[derive(Debug, Clone)]
pub struct ProfilerConfig {
    /// Enable function call tracing
    pub trace_calls: bool,
    /// Enable memory allocation tracking
    pub trace_memory: bool,
    /// Sample rate (1 = every call, 10 = every 10th call)
    pub sample_rate: u32,
    /// Output format
    pub output_format: OutputFormat,
}

impl Default for ProfilerConfig {
    fn default() -> Self {
        Self {
            trace_calls: true,
            trace_memory: false,
            sample_rate: 1,
            output_format: OutputFormat::Text,
        }
    }
}

/// Output format for profiling data
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OutputFormat {
    Text,
    Json,
    Flamegraph,
}

/// A single profiling event
#[derive(Debug, Clone)]
pub struct ProfileEvent {
    pub name: String,
    pub start_time: Instant,
    pub duration: Duration,
    pub call_count: u64,
    pub children: Vec<String>,
}

/// Function statistics
#[derive(Debug, Clone, Default)]
pub struct FunctionStats {
    pub name: String,
    pub call_count: u64,
    pub total_time: Duration,
    pub self_time: Duration,
    pub min_time: Duration,
    pub max_time: Duration,
    pub avg_time: Duration,
}

impl FunctionStats {
    pub fn new(name: &str) -> Self {
        Self {
            name: name.to_string(),
            call_count: 0,
            total_time: Duration::ZERO,
            self_time: Duration::ZERO,
            min_time: Duration::MAX,
            max_time: Duration::ZERO,
            avg_time: Duration::ZERO,
        }
    }

    pub fn record(&mut self, duration: Duration) {
        self.call_count += 1;
        self.total_time += duration;
        self.min_time = self.min_time.min(duration);
        self.max_time = self.max_time.max(duration);
        self.avg_time = self.total_time / self.call_count as u32;
    }
}

/// The main profiler state
#[derive(Debug)]
pub struct Profiler {
    config: ProfilerConfig,
    functions: HashMap<String, FunctionStats>,
    call_stack: Vec<(String, Instant)>,
    start_time: Instant,
    total_allocations: u64,
    total_bytes_allocated: u64,
}

impl Profiler {
    pub fn new(config: ProfilerConfig) -> Self {
        Self {
            config,
            functions: HashMap::new(),
            call_stack: Vec::new(),
            start_time: Instant::now(),
            total_allocations: 0,
            total_bytes_allocated: 0,
        }
    }

    /// Start profiling a function
    pub fn enter_function(&mut self, name: &str) {
        if !self.config.trace_calls {
            return;
        }
        self.call_stack.push((name.to_string(), Instant::now()));
    }

    /// End profiling a function
    pub fn exit_function(&mut self) {
        if !self.config.trace_calls {
            return;
        }
        
        if let Some((name, start)) = self.call_stack.pop() {
            let duration = start.elapsed();
            
            let stats = self.functions
                .entry(name)
                .or_insert_with(|| FunctionStats::new(""));
            stats.record(duration);
        }
    }

    /// Record a memory allocation
    pub fn record_allocation(&mut self, bytes: u64) {
        if !self.config.trace_memory {
            return;
        }
        self.total_allocations += 1;
        self.total_bytes_allocated += bytes;
    }

    /// Get profiling summary
    pub fn summary(&self) -> ProfilingSummary {
        let mut functions: Vec<_> = self.functions.values().cloned().collect();
        functions.sort_by(|a, b| b.total_time.cmp(&a.total_time));

        ProfilingSummary {
            total_time: self.start_time.elapsed(),
            functions,
            total_allocations: self.total_allocations,
            total_bytes_allocated: self.total_bytes_allocated,
        }
    }
}

/// Profiling summary for reporting
#[derive(Debug)]
pub struct ProfilingSummary {
    pub total_time: Duration,
    pub functions: Vec<FunctionStats>,
    pub total_allocations: u64,
    pub total_bytes_allocated: u64,
}
