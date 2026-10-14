# Optional C++ metadata codec benchmark

Engineering fixtures only; `codec.proto` is not the production protocol. Requires installed
Boost.JSON, CMake≥3.20 and matching Protobuf CONFIG package/runtime/protoc. No downloads.
From the project root:

```sh
cmake -S tools/protocol_bench -B artifacts/protocol-bench-build -DCMAKE_BUILD_TYPE=Release
cmake --build artifacts/protocol-bench-build -j4
artifacts/protocol-bench-build/sv-protocol-bench > artifacts/protocol-bench-v1.json
```

50k iterations ×7 batches; median batch means, fresh serialization/decode objects, interleaved
codec order, roundtrip and ID checks. Not a full production header, semantic validator, memory,
transport or end-to-end benchmark. Baseline, interpretation and migration proposal:
[PROTOBUF.md](../../docs/engineering/PROTOBUF.md).
