// Inspect emitted bound-setter thunks: native tags must not reserve conversion
// storage. Slot availability and target snapshots precede either call path.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
using namespace telemetry;

struct BoundSetterOwner {
    float readFloat() const noexcept;
    WriteResult writeFloat(float) noexcept;
    std::uint16_t readCount() const noexcept;
    WriteResult writeCount(std::uint16_t) noexcept;
};
extern BoundSetterOwner boundSetterOwner;
extern OwnerSlot<BoundSetterOwner> boundSetterOwnerSlot;
float boundSetterFreeRead() noexcept;
WriteResult boundSetterFreeWrite(float) noexcept;
struct BoundSetterReader { std::uint16_t operator()() const noexcept; };
struct BoundSetterWriter { WriteResult operator()(std::uint16_t) const noexcept; };
extern BoundSetterReader boundSetterReader;
extern BoundSetterWriter boundSetterWriter;
extern FunctionSlot<WriteResult(std::uint16_t) noexcept> boundSetterFunction;
extern ContextFunctionSlot<WriteResult(std::uint16_t) noexcept> boundSetterContext;
extern DelegateRefSlot<WriteResult(std::uint16_t) noexcept> boundSetterReference;
extern DelegateSlot<WriteResult(std::uint16_t) noexcept> boundSetterOwned;

extern "C" {
extern constexpr Field boundSetterRows[]{
    field<&BoundSetterOwner::readFloat, &BoundSetterOwner::writeFloat>("member float", "", boundSetterOwner).materialize(),
    field<&BoundSetterOwner::readCount, &BoundSetterOwner::writeCount>("member count", "", boundSetterOwner).materialize(),
    field<&boundSetterFreeRead, &boundSetterFreeWrite>("free", "").materialize(),
    field("borrowed", "", boundSetterReader, boundSetterWriter).materialize(),
    field<&BoundSetterOwner::readCount, &BoundSetterOwner::writeCount>("owner slot", "", boundSetterOwnerSlot).materialize(),
    field("function slot", "", boundSetterReader, boundSetterFunction).materialize(),
    field("context slot", "", boundSetterReader, boundSetterContext).materialize(),
    field("reference slot", "", boundSetterReader, boundSetterReference).materialize(),
    field("owned slot", "", boundSetterReader, boundSetterOwned).materialize(),
};
WriteResult bound_setter_member_float(const Scalar& value) noexcept { return boundSetterRows[0].set(value); }
WriteResult bound_setter_member_count(const Scalar& value) noexcept { return boundSetterRows[1].set(value); }
WriteResult bound_setter_free(const Scalar& value) noexcept { return boundSetterRows[2].set(value); }
WriteResult bound_setter_borrowed(const Scalar& value) noexcept { return boundSetterRows[3].set(value); }
WriteResult bound_setter_owner(const Scalar& value) noexcept { return boundSetterRows[4].set(value); }
WriteResult bound_setter_function(const Scalar& value) noexcept { return boundSetterRows[5].set(value); }
WriteResult bound_setter_context(const Scalar& value) noexcept { return boundSetterRows[6].set(value); }
WriteResult bound_setter_reference(const Scalar& value) noexcept { return boundSetterRows[7].set(value); }
WriteResult bound_setter_owned(const Scalar& value) noexcept { return boundSetterRows[8].set(value); }
}
