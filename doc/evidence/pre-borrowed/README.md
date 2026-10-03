# Pre-borrowed Stage20 evidence

This archive preserves exact tracked receipt/validator bytes from commit `595a65b158aa6f65dd4e2499fd08817a88f450d5`. It performs no new software or device execution. The receipts retain their original measured source HEAD, captured-input hashes, counts, scope and restoration records. They are historical evidence; they do not qualify the borrowed-result ABI or current source tree.

The [manifest](manifest.json) records every original path, Git blob ID, byte length and SHA-256. Copies are byte-identical to those Git blobs, without newline conversion. The source receipt paths may subsequently hold renewed evidence; this archive remains distinct.

| Original path | Archived copy | Captured source HEAD |
| --- | --- | --- |
| `tests/structured/mcu/local-receipt.json` | [offline/local-receipt.json](offline/local-receipt.json) | `01fd180ff3678d7c47066f8a728dc1e02f0f3973` |
| `tests/structured/mcu/receipt.py` | [offline/receipt.py](offline/receipt.py) | validator source at archive commit |
| `tests/structured/descriptor/h7s/receipt.json` | [h7s/descriptor/receipt.json](h7s/descriptor/receipt.json) | `01fd180ff3678d7c47066f8a728dc1e02f0f3973` |
| `tests/structured/descriptor/h7s/verify.py` | [h7s/descriptor/verify.py](h7s/descriptor/verify.py) | validator source at archive commit |
| `tests/structured/resources/h7s/receipt.json` | [h7s/resources/receipt.json](h7s/resources/receipt.json) | `01fd180ff3678d7c47066f8a728dc1e02f0f3973` |
| `tests/structured/resources/h7s/verify.py` | [h7s/resources/verify.py](h7s/resources/verify.py) | validator source at archive commit |
| `tests/structured/exchange/h7s/receipt.json` | [h7s/exchange/receipt.json](h7s/exchange/receipt.json) | `01fd180ff3678d7c47066f8a728dc1e02f0f3973` |
| `tests/structured/exchange/h7s/verify.py` | [h7s/exchange/verify.py](h7s/exchange/verify.py) | validator source at archive commit |
| `tests/structured/mcu/h7s/receipt.json` | [h7s/mcu/receipt.json](h7s/mcu/receipt.json) | `01fd180ff3678d7c47066f8a728dc1e02f0f3973` |
| `tests/structured/mcu/h7s/verify.py` | [h7s/mcu/verify.py](h7s/mcu/verify.py) | validator source at archive commit |

The validator copies retain their original location-dependent path logic. They are exact source records, not adapted installations at these archive paths. Historical checks must explicitly disable current-tree claims and supply the original retained artifacts where required. Receipt JSON and digests alone do not replace the corresponding binaries, UART logs, source snapshots, backups and readbacks.
