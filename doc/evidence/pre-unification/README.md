# Historical telemetry measurements

These retained files describe pre-unification source trees. They are not
evidence of executing the final library merely because validation passes.
Original sources remain retrievable at Git commit
`389c995083b4dc0a39cd8775b211ef765494240f` and the earlier recorded commits.

`stage14/` preserves the actual H7S receipt, result text and independent
validator from before source migration. Its source HEAD, dirty state,
195 LF input hashes, four image identities and full Flash restoration
remain unchanged. `local-receipt.json` is the earlier seven-role offline
proof, with no device execution. Links inside the original result text
were relative to `tests/structured/mcu/h7s`; they are historical references.

`stage08/` preserves earlier alignment/storage/dispatch measurements. The
standalone archived validator retains the original `verify` rules and its
mutation controls, extracted from the old runner. It has no compiler,
programmer or serial integration and does not import removed test helpers.

`stage09/`, `stage10/` and `stage11/` preserve the original Descriptor,
Values and Bind/Exchange H7S receipts byte for byte from that Git checkpoint.
Their recorded source identities, dirty states and input/image hashes are
historical. The matching final-source receipts live next to the active tests;
retaining both does not turn the earlier runs into final-source evidence.

```sh
python doc/evidence/pre-unification/stage14/verify.py --allow-stale-inputs --self-test
python doc/evidence/pre-unification/stage08/verify.py --receipt doc/evidence/pre-unification/stage08/final-receipt.json --self-test
```

The final [H7S proof](../../../tests/structured/mcu/h7s/RESULTS.md) and
[offline receipt](../../../tests/structured/mcu/local-receipt.json) are
generated from new runs against the final source paths. No historical
receipt HEAD or source hash is rewritten to make it look current.
