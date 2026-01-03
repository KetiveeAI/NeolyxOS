// REOX Profiler - Report Generation
// Formats profiling data for display

use super::{ProfilingSummary, FunctionStats, OutputFormat};
use std::fmt::Write;

/// Generate a text report
pub fn generate_text_report(summary: &ProfilingSummary) -> String {
    let mut output = String::new();

    writeln!(output, "").unwrap();
    writeln!(output, "=== REOX Profiling Report ===").unwrap();
    writeln!(output, "Total execution time: {:.3}ms", 
             summary.total_time.as_secs_f64() * 1000.0).unwrap();
    writeln!(output, "").unwrap();

    writeln!(output, "{:<30} {:>10} {:>12} {:>12} {:>12}",
             "Function", "Calls", "Total (ms)", "Self (ms)", "Avg (us)").unwrap();
    writeln!(output, "{:-<30} {:->10} {:->12} {:->12} {:->12}",
             "", "", "", "", "").unwrap();

    for func in &summary.functions {
        let total_ms = func.total_time.as_secs_f64() * 1000.0;
        let self_ms = func.self_time.as_secs_f64() * 1000.0;
        let avg_us = func.avg_time.as_secs_f64() * 1_000_000.0;

        writeln!(output, "{:<30} {:>10} {:>12.3} {:>12.3} {:>12.3}",
                 truncate(&func.name, 30),
                 func.call_count,
                 total_ms,
                 self_ms,
                 avg_us).unwrap();
    }

    if summary.total_allocations > 0 {
        writeln!(output, "").unwrap();
        writeln!(output, "Memory: {} allocations, {} bytes total",
                 summary.total_allocations,
                 summary.total_bytes_allocated).unwrap();
    }

    writeln!(output, "").unwrap();
    writeln!(output, "=============================").unwrap();

    output
}

/// Generate a JSON report
pub fn generate_json_report(summary: &ProfilingSummary) -> String {
    let mut output = String::new();

    writeln!(output, "{{").unwrap();
    writeln!(output, "  \"total_time_ms\": {:.3},",
             summary.total_time.as_secs_f64() * 1000.0).unwrap();
    writeln!(output, "  \"functions\": [").unwrap();

    for (i, func) in summary.functions.iter().enumerate() {
        let comma = if i < summary.functions.len() - 1 { "," } else { "" };
        writeln!(output, "    {{").unwrap();
        writeln!(output, "      \"name\": \"{}\",", func.name).unwrap();
        writeln!(output, "      \"calls\": {},", func.call_count).unwrap();
        writeln!(output, "      \"total_ms\": {:.3},", 
                 func.total_time.as_secs_f64() * 1000.0).unwrap();
        writeln!(output, "      \"avg_us\": {:.3}",
                 func.avg_time.as_secs_f64() * 1_000_000.0).unwrap();
        writeln!(output, "    }}{}", comma).unwrap();
    }

    writeln!(output, "  ],").unwrap();
    writeln!(output, "  \"memory\": {{").unwrap();
    writeln!(output, "    \"allocations\": {},", summary.total_allocations).unwrap();
    writeln!(output, "    \"bytes\": {}", summary.total_bytes_allocated).unwrap();
    writeln!(output, "  }}").unwrap();
    writeln!(output, "}}").unwrap();

    output
}

/// Generate flamegraph-compatible output (folded stacks)
pub fn generate_flamegraph_output(summary: &ProfilingSummary) -> String {
    let mut output = String::new();

    // Flamegraph format: stack;frame count
    for func in &summary.functions {
        let samples = (func.total_time.as_nanos() / 1_000_000) as u64; // 1ms = 1 sample
        if samples > 0 {
            writeln!(output, "{} {}", func.name, samples).unwrap();
        }
    }

    output
}

/// Format report based on output format
pub fn format_report(summary: &ProfilingSummary, format: OutputFormat) -> String {
    match format {
        OutputFormat::Text => generate_text_report(summary),
        OutputFormat::Json => generate_json_report(summary),
        OutputFormat::Flamegraph => generate_flamegraph_output(summary),
    }
}

fn truncate(s: &str, max_len: usize) -> String {
    if s.len() <= max_len {
        s.to_string()
    } else {
        format!("{}...", &s[..max_len - 3])
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn test_text_report() {
        let summary = ProfilingSummary {
            total_time: Duration::from_millis(100),
            functions: vec![
                FunctionStats {
                    name: "main".to_string(),
                    call_count: 1,
                    total_time: Duration::from_millis(100),
                    self_time: Duration::from_millis(50),
                    min_time: Duration::from_millis(100),
                    max_time: Duration::from_millis(100),
                    avg_time: Duration::from_millis(100),
                },
            ],
            total_allocations: 10,
            total_bytes_allocated: 1024,
        };

        let report = generate_text_report(&summary);
        assert!(report.contains("main"));
        assert!(report.contains("100.000"));
    }
}
