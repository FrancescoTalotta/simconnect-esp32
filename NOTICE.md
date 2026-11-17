# Attribution and license

This project is licensed under LGPL-3.0-or-later.

The SimConnect wire implementation is adapted from the user's earlier
`ARINC_TX_RX_OTA_GENERAL/main/simconnect_client.c` prototype and references
[node-simconnect](https://github.com/EvenAR/node-simconnect), by
Even Arneberg Rognlien and contributors, LGPL-3.0-or-later.
Reference revision: `2fca2ee3486e2e5972c2ecc3fe18592955e2c3fb` (4.2.1).
Upstream itself credits the Java jsimconnect implementation by lc0277 and mharj.
No independent/clean-room authorship is claimed.

The prior C prototype supplied the packet field layouts and numeric operations.
This adaptation replaces its connection-per-operation API with a single-owner
session, corrects exception dispatch, preserves fragmented input, bounds frames,
adds absolute deadlines and STRING256 decoding, and separates board/aircraft
logic into an example. The older prototype is left untouched outside this repo.

The example, tests, build wrappers, and documentation are supplied under the
same project license. ESP-IDF and its bundled components retain their respective
licenses and notices; they are not relicensed by this project.

See `LICENSE` for LGPLv3 and `COPYING` for GPLv3 incorporated by LGPLv3.
Commercial use is allowed subject to those terms. Firmware is statically linked;
when distributing it, supply required source/application material so recipients
can modify and relink the covered library, and installation information where
required. Preserve third-party notices. No warranty is provided.
