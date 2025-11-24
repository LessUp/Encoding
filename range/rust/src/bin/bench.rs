use rangecoder::{decode, encode};
use std::time::Instant;

fn make_test_data(size: usize) -> Vec<u8> {
    let mut v = Vec::with_capacity(size);
    for i in 0..size {
        v.push(((i as u32 * 31 + 7) & 0xFF) as u8);
    }
    v
}

fn main() {
    let size: usize = 1 << 20; // 1 MiB
    let iterations: usize = 20;

    let data = make_test_data(size);

    let start_enc = Instant::now();
    let mut encoded = Vec::new();
    for _ in 0..iterations {
        encoded = encode(&data).expect("encode failed");
    }
    let enc_dur = start_enc.elapsed();

    let start_dec = Instant::now();
    let mut decoded = Vec::new();
    for _ in 0..iterations {
        decoded = decode(&encoded).expect("decode failed");
    }
    let dec_dur = start_dec.elapsed();

    assert_eq!(decoded, data);

    let total_mb = (size as f64 * iterations as f64) / (1024.0 * 1024.0);
    let enc_secs = enc_dur.as_secs_f64();
    let dec_secs = dec_dur.as_secs_f64();

    println!("Rust range coder benchmark");
    println!("Input size: {} bytes", size);
    println!("Iterations: {}", iterations);
    println!("Encoded size (last run): {} bytes", encoded.len());
    println!(
        "Encode time: {:.6} s, throughput: {:.2} MiB/s",
        enc_secs,
        total_mb / enc_secs
    );
    println!(
        "Decode time: {:.6} s, throughput: {:.2} MiB/s",
        dec_secs,
        total_mb / dec_secs
    );
}
