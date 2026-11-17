# Provenance audit

## Decision

The project uses LGPL-3.0-or-later, matching `node-simconnect`. The earlier
noncommercial-license conflict is removed. Upstream adaptations can proceed
under LGPL with attribution and compliance; no separate noncommercial grant is
needed. The adaptation records the earlier client's source and upstream
attribution in [NOTICE.md](../NOTICE.md). Missing notices do not establish independent authorship.
This audit is a technical inventory, not a legal opinion.

## Inspected references

| Reference | Observation | Distribution decision |
| --- | --- | --- |
| Earlier local `simconnect_client.c` and `.h` | User identifies the project as based on `node-simconnect`; files have no attribution/license header | Adapted packet layouts and operations; old files left unchanged |
| `EvenAR/node-simconnect` | Package metadata declares LGPL-3.0-or-later | Attribution and reference revision recorded; compatible project license |
| Microsoft SDK documentation | Used to describe API behavior | Linked; no SDK binaries or headers bundled |
| FlyByWire documentation | Used to identify aircraft variables | Linked; no aircraft source bundled |
| GNU LGPLv3 license text | Standard published license text | Included unmodified as `LICENSE`; project permits later versions |
| GNU GPLv3 license text | Terms incorporated by LGPLv3 | Included as `COPYING` |

Earlier source, outside this independent repository (personal filesystem paths
omitted from the public record):

```text
ARINC_TX_RX_OTA_GENERAL/main/simconnect_client.c
ARINC_TX_RX_OTA_GENERAL/main/simconnect_client.h
```

The parent Git repository has no file history for that source path at the time
of inspection. Its absence is not proof that no history or authors exist
elsewhere. The inspected files provide no authorship chain. No relicensing to
noncommercial terms is now sought.

SHA-256 of the inspected prototype:

```text
simconnect_client.c aa50a5380f1e554c7bd76f4244d32e51067b3adae2670f51eafa91511d5a225e
simconnect_client.h 7b25f55880dd8a84fa42254ba20fa5c29033ea01c055a912503b2c49365020a0
```

Upstream baseline reviewed: version 4.2.1, commit
`2fca2ee3486e2e5972c2ecc3fe18592955e2c3fb`:

- [Repository](https://github.com/EvenAR/node-simconnect)
- [Pinned package metadata](https://github.com/EvenAR/node-simconnect/blob/2fca2ee3486e2e5972c2ecc3fe18592955e2c3fb/package.json)
- [Pinned receive definitions](https://github.com/EvenAR/node-simconnect/blob/2fca2ee3486e2e5972c2ecc3fe18592955e2c3fb/src/SimConnectSocket.ts)

## Findings in the earlier client

Line numbers refer to the inspected local version, not files in this repository.

| Severity | Location | Finding |
| --- | --- | --- |
| High | `simconnect_client.c:27` | Exception receive ID is 5 instead of upstream's 1 |
| High | `:140–153,229–259` | Timeout after a partial receive loses framing progress |
| High | `:246–248` | Unbounded frame-size allocation |
| High | `:295–297` | OPEN name read lacks payload-length validation |
| High | `:331–363` | Exception polling conflates timeout, disconnect, and failure |
| Medium | `:577–586,806–824` | Persistent send paths do not dispatch asynchronous exceptions |
| Medium | `:671–763` | Listener has no public stop operation |
| Medium | `:399–400` | Server epsilon always zero; threshold filtering is local only |
| Medium | `main/CMakeLists.txt` | Client source is absent from the component source list |

The old public data model is numeric FLOAT64 only. Tagged packets, event
subscriptions, Input Events, client-data bridging, and H-event support are not
implemented. Session thread safety is unspecified.

The new library addresses these findings with a persistent single-owner API,
explicit polling, bounded reusable receive buffer, retained partial frames,
absolute deadlines, exception/quit dispatch, and cancellable requests. It adds
STRING256 decoding for TITLE identity. It is not a source-compatible copy of
the old prototype's convenience API.

## Import under LGPL

1. Record the upstream reference revision and the earlier client's derivation.
2. Preserve the relevant upstream copyright and license notices; record local
   modifications without inventing or removing authorship.
3. Identify any other incorporated source and check its terms.
4. Include copies of both GNU GPLv3 and LGPLv3 with library/firmware distributions.
5. For statically linked firmware, provide the source/application material and
   installation information required by the license so recipients can modify
   and relink the covered library.

This is an upstream-based port, not a clean-room implementation.

## Validation and distribution

- Attribution: recorded in source and NOTICE; both license texts included.
- Packet tests: synthetic, authored for this project, not captured SDK/simulator data.
- Host tests: actual C implementation, fragmentation/timeouts/EOF/errors/serialization.
- Build: example compiles for ESP32-S3 on the installed SDK; see
  [compatibility evidence](compatibility.md).
- Device/simulator: still pending; do not advertise hardware-tested support.
- Distribution: preserve notices and supply required source/relinking material;
  do not publish configured credentials or claim a clean-room implementation.
