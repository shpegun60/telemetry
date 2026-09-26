// Review repro (slots): compiled twice, once as SUFFIX=a and once as SUFFIX=b,
// each time with a different tiny_delegate configuration (HEAP_FALLBACK, NDEBUG,
// TINY_DELEGATE_ASSERT). Both objects link into one program (MacroOdrMain.cpp).
#include "MacroOdrShared.h"

#define REVIEW_CAT2(a, b) a##b
#define REVIEW_CAT(a, b) REVIEW_CAT2(a, b)

extern "C" void REVIEW_CAT(odr_bind_, SUFFIX)() noexcept
{
    odrOwnedRead.bind(OdrReader{42.f});
    odrOwnedWrite.bind(OdrWriter{&odrStore});
    odrRefRead.bind(odrBorrowed);
}
extern "C" float REVIEW_CAT(odr_read_, SUFFIX)() noexcept
{
    return odrRows.read<0>().value_or(-1.f) + odrRows.read<1>().value_or(-100.f);
}
extern "C" telemetry::WriteResult REVIEW_CAT(odr_write_, SUFFIX)(float v) noexcept
{
    return odrRows.write<0>(v);
}
