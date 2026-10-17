# Protocol fixtures

`fixtures/` holds binary samples of every control message and media packet, both valid and
malformed. The C++ (GoogleTest) and Swift test suites decode the same files, so both apps are
held to one wire format. The format itself is specified in [`docs/PROTOCOL.md`](../docs/PROTOCOL.md).

Fixtures are added in Phase 3. Files here are treated as binary by Git (`.gitattributes`).
