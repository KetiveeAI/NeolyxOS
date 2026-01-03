// REOX Profiler - Instrumentation
// Code generation for profiling hooks

use crate::parser::{Ast, Decl, FnDecl};

/// Instrumentation mode
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InstrumentMode {
    /// No instrumentation
    None,
    /// Function entry/exit only
    Functions,
    /// Full instrumentation (calls, allocations, branches)
    Full,
}

/// Generate instrumented C code with profiling hooks
pub struct Instrumentor {
    mode: InstrumentMode,
}

impl Instrumentor {
    pub fn new(mode: InstrumentMode) -> Self {
        Self { mode }
    }

    /// Generate profiling header includes
    pub fn emit_header(&self) -> String {
        if self.mode == InstrumentMode::None {
            return String::new();
        }

        r#"
// Profiling macros
#ifndef RX_PROFILE_ENABLED
#define RX_PROFILE_ENABLED 1
#endif

#if RX_PROFILE_ENABLED
#include <time.h>
#include <stdio.h>

typedef struct {
    const char* name;
    clock_t start;
    unsigned long calls;
    double total_ms;
} rx_profile_entry;

#define RX_MAX_PROFILE_ENTRIES 256
static rx_profile_entry rx_profile_data[RX_MAX_PROFILE_ENTRIES];
static int rx_profile_count = 0;

static inline int rx_profile_find(const char* name) {
    for (int i = 0; i < rx_profile_count; i++) {
        if (rx_profile_data[i].name == name) return i;
    }
    if (rx_profile_count < RX_MAX_PROFILE_ENTRIES) {
        rx_profile_data[rx_profile_count].name = name;
        rx_profile_data[rx_profile_count].calls = 0;
        rx_profile_data[rx_profile_count].total_ms = 0;
        return rx_profile_count++;
    }
    return -1;
}

#define RX_PROFILE_ENTER(name) \
    static const char* _rx_fn_name = name; \
    clock_t _rx_start = clock();

#define RX_PROFILE_EXIT() \
    do { \
        clock_t _rx_end = clock(); \
        int _rx_idx = rx_profile_find(_rx_fn_name); \
        if (_rx_idx >= 0) { \
            rx_profile_data[_rx_idx].calls++; \
            rx_profile_data[_rx_idx].total_ms += \
                1000.0 * (_rx_end - _rx_start) / CLOCKS_PER_SEC; \
        } \
    } while(0)

static void rx_profile_report(void) {
    fprintf(stderr, "\n=== REOX Profile Report ===\n");
    fprintf(stderr, "%-30s %10s %12s %12s\n", 
            "Function", "Calls", "Total (ms)", "Avg (ms)");
    fprintf(stderr, "%-30s %10s %12s %12s\n",
            "--------", "-----", "----------", "--------");
    for (int i = 0; i < rx_profile_count; i++) {
        double avg = rx_profile_data[i].calls > 0 
            ? rx_profile_data[i].total_ms / rx_profile_data[i].calls 
            : 0;
        fprintf(stderr, "%-30s %10lu %12.3f %12.6f\n",
                rx_profile_data[i].name,
                rx_profile_data[i].calls,
                rx_profile_data[i].total_ms,
                avg);
    }
    fprintf(stderr, "===========================\n");
}

// Auto-report at exit
__attribute__((destructor))
static void rx_profile_cleanup(void) {
    rx_profile_report();
}

#else
#define RX_PROFILE_ENTER(name)
#define RX_PROFILE_EXIT()
#endif
"#.to_string()
    }

    /// Instrument a function with profiling hooks
    pub fn instrument_function(&self, fn_decl: &FnDecl) -> (String, String) {
        if self.mode == InstrumentMode::None {
            return (String::new(), String::new());
        }

        let enter = format!("RX_PROFILE_ENTER(\"{}\");", fn_decl.name);
        let exit = "RX_PROFILE_EXIT();".to_string();

        (enter, exit)
    }
}

/// Instrumentation options for code generation
#[derive(Debug, Clone)]
pub struct InstrumentOptions {
    pub mode: InstrumentMode,
    pub sample_rate: u32,
    pub track_allocations: bool,
}

impl Default for InstrumentOptions {
    fn default() -> Self {
        Self {
            mode: InstrumentMode::None,
            sample_rate: 1,
            track_allocations: false,
        }
    }
}
