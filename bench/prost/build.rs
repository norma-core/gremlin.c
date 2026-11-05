fn main() {
    let corpus = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../descriptors/tests/proto_corpus"
    );
    let protos = ["benchmark.proto", "unittest.proto", "unittest_import.proto"];
    // prost-build does not watch the protos, and they are outside the crate.
    for p in protos {
        println!("cargo::rerun-if-changed={corpus}/{p}");
    }
    prost_build::compile_protos(&protos, &[corpus]).unwrap();
}
