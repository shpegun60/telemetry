# Shared callback utilities

These headers contain the existing fixed-storage callable slots, callable checks
and compiler attributes. They keep their established `telemetry` namespaces,
class layouts and macro names for compatibility. They require only the standard
library and the bundled tiny_delegate header; they do not include Model,
Registry, Codec, Resource or Client.

Include `<common/DelegateSlot.hpp>` for an owning callback or
`<common/DelegateRefSlot.hpp>` for a borrowed callback. The original telemetry
slot and detail headers forward here. Storage and borrowed objects must outlive
calls, and an executing slot cannot be reset or rebound.
