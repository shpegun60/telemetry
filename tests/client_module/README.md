# Client module isolation

`run.py --build-dir <external> --cxx g++` copies only canonical client-core,
common and tiny_delegate headers into an isolated include tree. Every public
core/common header is compiled independently. The runtime smoke checks typed
decode, FIFO callbacks, payload ownership, cancellation, drain and both delegate
slot modes. A dependency manifest refuses access to repository library headers.
The same smoke also compiles directly from the source include root. Negative
controls prove the removed client include paths no longer resolve.

Outputs, logs, compiler identity and source snapshots remain external. No
Model, Registry, native Codec, Resource, generated SDK or hardware is required.
