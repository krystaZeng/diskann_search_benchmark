# Artifact Checksum Canonicalization v1

## 1. Algorithms

- Payload digest: SHA-256 over the exact stored payload bytes, with no decoding, newline change,
  decompression, or path contribution.
- Aggregate digest: SHA-256 over the UTF-8 bytes emitted by `diskann-artifact-c14n-v1` from the
  aggregate projection below.
- Text form: exactly 64 lowercase hexadecimal characters.

Implementations use OpenSSL 3.x EVP in C++ and `hashlib.sha256` in Python. Cross-language golden
tests must produce identical bytes and digests.

## 2. Restricted Canonical JSON Profile

The aggregate projection may contain only:

- UTF-8 JSON strings;
- booleans;
- signed or unsigned integers within the schema-defined range;
- arrays;
- objects whose keys are UTF-8 strings.

`null`, floating-point numbers, NaN/infinity, duplicate object keys, invalid UTF-8, and Unicode
normalization performed by the serializer are forbidden.

Serialization rules are byte-for-byte:

1. Encode the document as UTF-8 without BOM.
2. Sort object keys by unsigned UTF-8 byte sequence in ascending lexicographic order.
3. Preserve array order exactly.
4. Emit `true` and `false` in lowercase.
5. Emit integers in base 10 with no leading zeros; zero is `0`; negative zero is forbidden.
6. Escape only quotation mark, reverse solidus, and control characters. Use the shortest standard
   JSON escape for `\b`, `\t`, `\n`, `\f`, and `\r`; encode other U+0000..U+001F characters as
   lowercase `\u00xx`. Do not escape `/` or non-ASCII UTF-8 characters.
7. Emit no whitespace outside strings and no trailing newline.

The profile identifier `diskann-artifact-c14n-v1` is part of Artifact interpretation metadata.

## 3. Aggregate Projection

The projection contains exactly these keys:

```json
{
  "artifact_type": "...",
  "canonicalization": "diskann-artifact-c14n-v1",
  "capabilities": ["..."],
  "dataset_checksum": "...",
  "format_version": "...",
  "id_space": "local",
  "payloads": [
    {"byte_length": 0, "role": "...", "sha256": "..."}
  ],
  "schema_version": "1.0.0",
  "source_artifacts": [
    {"aggregate_checksum": "...", "artifact_type": "...", "format_version": "..."}
  ],
  "structural": {}
}
```

Rules for projection fields:

- `capabilities` is sorted by unsigned UTF-8 bytes and contains no duplicates.
- `payloads` preserves the role order defined by the concrete Artifact schema. `path` and
  `media_type` are deliberately absent; role, length, and exact digest are present.
- `source_artifacts` preserves the semantic order defined by the concrete Artifact: dataset first,
  Partition next, then LocalGraph sources by ascending PartitionId.
- `structural` contains every field that changes interpretation or validator invariants, including
  counts, dimensions, metric, Partition records, degree declarations, and source Partition ID.
- `aggregate_checksum` itself is absent.

## 4. Explicit Exclusions

The following never enter the aggregate projection:

- absolute or workspace-relative filesystem paths;
- timestamps and run IDs;
- repository state, compiler, build flags, environment, and host information;
- BuildManifest or EvaluationReport identity/content;
- runtime metrics, latency, memory, or counters;
- failure evidence and quarantine paths;
- the aggregate checksum field itself.

Exclusion from Artifact identity does not remove a field's BuildManifest or report obligation.

## 5. Validation Procedure

1. Open each declared payload without following a path outside its Artifact directory.
2. Stream exact bytes through SHA-256; verify declared length and digest.
3. Decode and semantically validate the payload according to `media_type`/format.
4. Construct the projection from validated typed values, not by deleting keys from untrusted JSON.
5. Serialize with `diskann-artifact-c14n-v1` and recompute aggregate SHA-256.
6. Compare in constant-time-capable digest form where available; report expected and observed
   values without altering the candidate.

Golden tests include empty strings/arrays, UTF-8 keys and values, control escaping, integer bounds,
payload order changes, excluded path changes, and C++/Python byte equivalence.
