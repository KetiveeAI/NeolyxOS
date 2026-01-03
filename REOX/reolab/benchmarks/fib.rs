// Fibonacci Benchmark - Rust Version
fn fib(n: i64) -> i64 {
    if n <= 1 { n } else { fib(n - 1) + fib(n - 2) }
}

fn main() {
    let n: i64 = 35;
    let result = fib(n);
    println!("fib({}) = {}", n, result);
}
