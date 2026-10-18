# Generated DTOs with direct Encoded dispatch

[main.cpp](main.cpp) checks the native descriptor header, uses the generated
standalone codec to prepare a DTO and invokes `encoded::callService` on the
existing native ModelView. Caller-owned wire buffers and Workspace are explicit.
Only after a successful operation does it use the decoded response.

Build `codegen_encoded` using the [shared CMake example](../codegen_cpp/README.md).
This target links the native ABI/Encoded adapter and has no RpcClient,
RpcMailbox or Resource protocol dependency. Native DTOs retain their own types;
canonical bytes are the boundary with generated DTOs. See
[GeneratedCppApi](../../doc/GeneratedCppApi.md).
