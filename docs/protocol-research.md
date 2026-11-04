# Protocol research

Aural's protocol notes combine WH-1000XM4 packet captures with static analysis
of Sony Sound Connect 13.3.0 using [REA](https://github.com/morluto/rea).
Python scripts extracted enum values from the app's smali disassembly.

## V2 byte catalog

[sony-v2-byte-catalog.json](sony-v2-byte-catalog.json) contains 34 enums and
539 command and selector values. It covers symbols used across Sony's app;
each headset reports which functions it supports.

- `artifact_input_sha256` identifies the input APKS file.
- `extraction_evidence_id` identifies the saved REA extraction record.
- `source_path` is relative to the disassembly's `smali` directory.
- `sha256` identifies that source file.
- Each value has its enum name, decimal byte, hexadecimal byte and source line.

The input APKS SHA-256 is
`7901e52cf38c5ebd0d5be760e2b105df7ed7ab85c8e732dd4481c910f8fda31e`.
The published values and source hashes match the saved REA catalog. Local
machine paths have been replaced with relative source paths.

The APK, disassembly and full REA records are kept outside this repository.
Evidence IDs in the protocol notes refer to those records. The catalog contains
names and byte mappings, not method implementations or live headset results.

See [V1 protocol notes](../PROTOCOL.md) for XM4 captures and
[V2 research](../PROTOCOL-MULTIMODEL.md) for startup and message layouts.
