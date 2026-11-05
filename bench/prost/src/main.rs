use std::hint::black_box;
use std::io::Write;
use std::process::exit;
use std::time::Instant;

use prost::Message;

#[allow(dead_code, clippy::all)]
mod benchmark {
    include!(concat!(env!("OUT_DIR"), "/benchmark.rs"));
}
#[allow(dead_code, clippy::all)]
mod protobuf_unittest {
    include!(concat!(env!("OUT_DIR"), "/protobuf_unittest.rs"));
}
#[allow(dead_code, clippy::all)]
mod protobuf_unittest_import {
    include!(concat!(env!("OUT_DIR"), "/protobuf_unittest_import.rs"));
}

use benchmark::DeepNested;
use protobuf_unittest::TestAllTypes;
use protobuf_unittest::test_all_types::OneofField;

fn load(path: &str) -> Vec<u8> {
    std::fs::read(path).unwrap_or_else(|_| {
        eprintln!("cannot read {path}");
        exit(1)
    })
}

// BENCH_ONLY=workload/op runs that measurement alone (bench/allocs.sh).
fn selected(workload: &str, op: &str) -> bool {
    match std::env::var("BENCH_ONLY") {
        Ok(only) => only == format!("{workload}/{op}"),
        Err(_) => true,
    }
}

// As BENCH in bench.h: 100 warm-up runs, then n timed ones; f gets the iteration.
fn bench(workload: &str, op: &str, n: usize, mut f: impl FnMut(usize)) {
    if !selected(workload, op) {
        return;
    }
    for i in 0..100 {
        f(i);
    }
    let t = Instant::now();
    for i in 0..n {
        f(i);
    }
    let ns = t.elapsed().as_nanos() as f64;
    let mut out = std::io::stdout().lock();
    writeln!(out, "{workload} {op} {:.1}", ns / n as f64).unwrap();
    out.flush().unwrap();
}

fn bench_deep(data: &[u8], n: usize) {
    let mut out = [0u8; 4096];
    let msg = DeepNested::decode(data).unwrap_or_else(|_| {
        eprintln!("prost: cannot parse deep_nested");
        exit(1)
    });
    if msg.encode_to_vec() != data {
        eprintln!("prost: deep_nested does not encode back to the same bytes");
        exit(1);
    }
    if data.len() > out.len() {
        eprintln!("prost: deep_nested does not fit the buffer");
        exit(1);
    }

    bench("deep", "marshal", n, |_| {
        black_box(msg.encode(&mut &mut out[..]).is_ok());
    });

    bench("deep", "unmarshal", n, |_| {
        black_box(DeepNested::decode(black_box(data)).is_ok());
    });

    bench("deep", "lazy", n, |_| {
        let m = DeepNested::decode(black_box(data)).unwrap();
        black_box(m.root_id);
        black_box(m.root_name.len());
        black_box(m.active);
    });

    bench("deep", "deep", n, |_| {
        let m = DeepNested::decode(black_box(data)).unwrap();
        let l1 = m.nested.as_ref();
        let l2 = l1.and_then(|x| x.nested.as_ref());
        let l3 = l2.and_then(|x| x.nested.as_ref());
        black_box(m.root_id);
        black_box(l1.map_or(0, |x| x.id));
        black_box(l2.map_or(0, |x| x.id));
        black_box(l3.map_or(0, |x| x.id));
        black_box(l3.and_then(|x| x.nested.as_ref()).map_or(0, |x| x.value));
    });
}

fn bench_golden(data: &[u8], n: usize) {
    let mut out = [0u8; 4096];
    // prost drops unknown fields while parsing, as the others do before marshal.
    let mut msg = TestAllTypes::decode(data).unwrap_or_else(|_| {
        eprintln!("prost: cannot parse golden_message");
        exit(1)
    });
    // Room for the updated values, which take a few more bytes than the file's.
    if msg.repeated_nested_message.len() < 2 || msg.encoded_len() + 64 > out.len() {
        eprintln!("prost: golden_message does not fit the benchmark");
        exit(1);
    }

    bench("golden", "marshal", n, |bench_i| {
        let i = (bench_i & 1023) as i32;
        msg.optional_int32 = Some(101 + i);
        msg.optional_nested_message.get_or_insert_default().bb = Some(118 + i);
        msg.repeated_nested_message[0].bb = Some(218 + i);
        msg.repeated_nested_message[1].bb = Some(318 + i);
        msg.oneof_field = Some(OneofField::OneofUint32((601 + i) as u32));
        black_box(msg.encode(&mut &mut out[..]).is_ok());
    });

    bench("golden", "unmarshal", n, |_| {
        black_box(TestAllTypes::decode(black_box(data)).is_ok());
    });

    bench("golden", "lazy", n, |_| {
        let m = TestAllTypes::decode(black_box(data)).unwrap();
        black_box(m.optional_int32());
        black_box(m.optional_int64());
        black_box(m.optional_string().len());
    });

    bench("golden", "deep", n, |_| {
        let m = TestAllTypes::decode(black_box(data)).unwrap();
        black_box(m.optional_int32());
        black_box(m.optional_string().len());
        black_box(m.optional_nested_message.as_ref().map_or(0, |x| x.bb()));
        black_box(m.optional_foreign_message.as_ref().map_or(0, |x| x.c()));
        black_box(m.optional_import_message.as_ref().map_or(0, |x| x.d()));
    });
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 4 {
        eprintln!(
            "usage: {} iterations deep_nested.bin golden_message",
            args[0]
        );
        exit(2);
    }
    let n = match args[1].parse::<usize>() {
        Ok(v) if v > 0 => v,
        _ => 1_000_000,
    };
    bench_deep(&load(&args[2]), n);
    bench_golden(&load(&args[3]), n);
}
