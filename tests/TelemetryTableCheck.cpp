// Behavioral parity of native local/global tables and dynamic descriptors.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include "serialization/TelemetryCommandJson.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
using namespace telemetry;
namespace {
int checks=0, failures=0;
void expect(bool ok, const char* label) { ++checks; if(!ok) {++failures; std::printf("FAIL %s\n",label);} }
enum class Mode : std::uint16_t { Off, Auto, Manual };
enum class Wide : std::uint16_t { None=0, Large=2000 };
enum Legacy { Low=-2, High=2 };
template <class T> struct Owner {
    T value{}; int reads=0, writes=0;
    T read() noexcept { ++reads; return value; }
    WriteResult write(T next) noexcept {++writes;value=next;return WriteResult::Applied;}
};
Owner<float> owner;
Owner<Mode> modeOwner;
Owner<Wide> wideOwner;
Owner<Legacy> legacyOwner;
constexpr FieldTable values{
    field<&Owner<float>::read,&Owner<float>::write>("limit","V",owner,limits(2.f,-5.f,10.f)),
    field<&Owner<Mode>::read,&Owner<Mode>::write>("mode","",modeOwner),
    reservedField(),
    field<&Owner<Wide>::read,&Owner<Wide>::write>("wide","",wideOwner,enumSpec<Wide::None,Wide::Large>()),
    field<&Owner<Legacy>::read,&Owner<Legacy>::write>("legacy","",legacyOwner)};
constexpr FieldTable empty{};
constexpr FieldCatalogTable registry{group("empty",empty),group("values",values)};
enum class ValuePosition : std::uint64_t {Limit,Mode,Reserved,Wide,Legacy};
enum class NarrowPosition : std::int8_t {First=0};
enum PlainPosition {FirstPosition=0};
enum class BoolPosition : bool {First=false,Second=true};
static_assert(std::is_same_v<decltype(values.read<ValuePosition::Limit>()),std::optional<float>>);
static_assert(std::is_same_v<decltype(values.read<ValuePosition::Limit,double>()),std::optional<double>>);
static_assert(std::is_same_v<decltype(values.write<ValuePosition::Limit>(2)),WriteResult>);
constexpr Catalog rawGroups[]={{"empty",nullptr,0},{"values",values.data(),values.size()}};
constexpr auto oldBinding=CatalogIndex::bind<rawGroups>();
static_assert(sizeof(values)==5*sizeof(Field) && alignof(decltype(values))==alignof(Field));
static_assert(std::is_same_v<decltype(values.read<1>()),std::optional<std::uint16_t>>);
static_assert(std::is_same_v<decltype(registry.read<makeId(1,0)>()),std::optional<float>>);
static_assert(registry.find(makeId(1,4))==&values[4] && registry.find(makeId(0,0))==nullptr);
static_assert(telemetryAbiVersion==8 && sizeof(Command)==5*sizeof(void*));

float freeValue=1;
float freeRead() noexcept {return freeValue;}
WriteResult freeWrite(float v) noexcept {freeValue=v;return WriteResult::Busy;}
int converterCopies=0,converterCalls=0;
template<class Function,Function Target>
struct CallbackConversion {
    CallbackConversion() noexcept = default;
    // Copying is legal at the public call boundary, but the factory body must
    // not make another potentially throwing copy just to obtain the pointer.
    CallbackConversion(const CallbackConversion&) noexcept(false) {++converterCopies;}
    Function operator+() const noexcept {return Target;}
    operator Function() const noexcept {++converterCalls;return Target;}
};
constexpr FieldTable functions{
    field<&freeRead,&freeWrite>("nttp",""),
    field("function","",freeRead,freeWrite),
    field("address","",&freeRead,&freeWrite),
    field("lambda","",[]() noexcept {return freeValue;},[](float x) noexcept {return freeWrite(x);}),
    field("plus","",+[]() noexcept {return freeValue;},+[](float x) noexcept {return freeWrite(x);})};
template <std::size_t... I> void checkFunctions(std::index_sequence<I...>) {
    (expect(functions.write<I>(3)==WriteResult::Busy && functions.read<I>()==3.f,"callback forms preserve results"),...);
}

template<class To,class From> void compare(From input) {
    Owner<To> source;
    const FieldTable table{field<&Owner<To>::read,&Owner<To>::write>("n","",source)};
    const FieldCatalogTable groups{group("numbers",table)};
    const auto typed=groups.template write<0>(input);
    const To first=source.value;
    const int called=source.writes;
    source.value={};source.writes=0;
    const auto dynamic=groups.write(FieldId{0},input);
    expect(typed==dynamic && source.writes==called
           && (typed!=WriteResult::Applied || source.value==first),"native/dynamic conversion and side effects agree");
    if(typed==WriteResult::Applied) {
        expect(table.template read<0>()==table[0].template read<To>(),"native/dynamic reads agree");
        expect(groups.template read<0,double>()==table[0].template read<double>(),"global read conversion agrees");
    }
}
template<class T> void matrix() {
    compare<T>(0);compare<T>(1);compare<T>(-1);compare<T>(12.7);compare<T>(-0.9);
    compare<T>(std::numeric_limits<std::int64_t>::min());
    compare<T>(std::numeric_limits<std::uint64_t>::max());
    compare<T>(std::numeric_limits<float>::max());
    compare<T>(std::numeric_limits<double>::max());
    compare<T>(std::numeric_limits<double>::infinity());
    compare<T>(std::numeric_limits<double>::quiet_NaN());
    compare<T>(Scalar::fromU16(12));compare<T>(Scalar::null());
}
CommandResult configure(float v,Mode mode) noexcept {
    owner.value=v;modeOwner.value=mode;return CommandResult::Accepted;
}
constexpr CommandTable actions{command<&configure>("configure",arg<0>("v","",2.f,-5.f,10.f)),reservedCommand()};
constexpr CommandTable noActions{};
constexpr CommandCatalogTable commands{group("empty",noActions),group("actions",actions)};
struct Prefix { int padding = 42; };
struct Base {
    float value = 6;
    float read() const & noexcept { return value; }
    WriteResult write(float v) & noexcept { value = v; return WriteResult::Applied; }
};
struct Derived : Prefix, Base {};
constexpr auto prebuilt = commandArgs(arg<0>("v","",2.f,-5.f,10.f));
constexpr CommandTable prebuiltActions{command<&configure>("configure",prebuilt)};

// These unscoped enums exercise both partial and completely empty automatic
// scans. Explicit dictionaries must control every path, including direct set.
enum FixedWide : std::uint16_t { FixedOff=0, FixedHigh=1000, FixedMax=2000 };
enum UnfixedWide { UnfixedHigh=1000, UnfixedMax=2000 };
enum UnfixedNegative { NegativeLow=-2000, NegativeHigh=-1000 };
template<class E> Owner<E> enumOwner;
template<class E> E readEnum() noexcept { return enumOwner<E>.read(); }
template<class E> WriteResult writeEnum(E value) noexcept { return enumOwner<E>.write(value); }
constexpr auto wideCodes=enumSpec<FixedOff,FixedHigh,FixedMax>();
constexpr FieldTable explicitFree{
    field<&readEnum<FixedWide>,&writeEnum<FixedWide>>("fixed","",wideCodes)};
constexpr FieldTable explicitMember{
    field<&Owner<UnfixedWide>::read,&Owner<UnfixedWide>::write>(
        "unfixed","",enumOwner<UnfixedWide>,enumSpec<UnfixedHigh,UnfixedMax>())};
constexpr FieldTable explicitSubset{
    field<&readEnum<FixedWide>,&writeEnum<FixedWide>>("subset","",enumSpec<FixedHigh,FixedMax>())};
static_assert(explicitMember[0].declaredType.defaultValue().get<std::underlying_type_t<UnfixedWide>>() == 1000);

template<class E,class Table>
void checkExplicitEnum(const Table& table,Owner<E>& source,int minimum,int maximum) {
    using Raw=std::underlying_type_t<E>;
    const FieldCatalogTable catalog{group("enum",table)};
    for(const int input : {minimum, (minimum+maximum)/2, maximum}) {
        const int before=source.writes;
        expect(table.template write<0>(input)==WriteResult::Applied,
               "explicit enum local native write accepts selected interval");
        expect(catalog.template write<makeId(0,0)>(input)==WriteResult::Applied,
               "explicit enum global native write accepts selected interval");
        expect(catalog.write(makeId(0,0),input)==WriteResult::Applied,
               "explicit enum dynamic write accepts selected interval");
        expect(table[0].set(Scalar::from(static_cast<Raw>(input)))==WriteResult::Applied,
               "explicit enum direct setter uses the selected dictionary");
        expect(source.writes==before+4 && static_cast<Raw>(source.value)==static_cast<Raw>(input),
               "explicit enum invokes once per accepted write");
        expect(table.template read<0,double>()==double(input)
               && table[0].template read<double>()==double(input),
               "explicit enum native and dynamic reads agree");
    }
    const int before=source.writes;
    for(const int input : {minimum-1,maximum+1}) {
        expect(table.template write<0>(input)==WriteResult::InvalidValue
               && catalog.template write<makeId(0,0)>(input)==WriteResult::InvalidValue
               && catalog.write(makeId(0,0),input)==WriteResult::InvalidValue,
               "explicit enum bounds reject before all descriptor writes");
        expect(table[0].set(Scalar::from(static_cast<Raw>(input)))==WriteResult::InvalidValue,
               "explicit enum direct setter guards casts outside selected interval");
    }
    expect(table[0].set(Scalar::null())==WriteResult::InvalidValue && source.writes==before,
           "invalid enum writes never reach the callback");
}

void checkExplicitEnums() {
    checkExplicitEnum(explicitFree,enumOwner<FixedWide>,0,2000);
    checkExplicitEnum(explicitMember,enumOwner<UnfixedWide>,1000,2000);
    checkExplicitEnum(explicitSubset,enumOwner<FixedWide>,1000,2000);
    Owner<UnfixedNegative> negative;
    auto read=[&negative]() noexcept {return negative.read();};
    auto write=[&negative](UnfixedNegative value) noexcept {return negative.write(value);};
    const FieldTable borrowed{field("negative","",read,write,enumSpec<NegativeLow,NegativeHigh>())};
    checkExplicitEnum(borrowed,negative,-2000,-1000);
}

void checkNamedPositions() {
    Owner<float> source;
    const FieldTable table{
        field<&Owner<float>::read,&Owner<float>::write>("value","",source,limits(2.f,-5.f,10.f)),
        reservedField()};
    enum class Position : std::uint64_t {Value,Reserved};
    constexpr std::size_t namedZero=0;
    expect(table.write<Position::Value>(3)==WriteResult::Applied && source.writes==1,
           "scoped enum position selects exactly one setter");
    expect(table.read<Position::Value>()==3.f && table.read<Position::Value,double>()==3.0
           && source.reads==2,"scoped enum reads retain native/requested types and side effects");
    expect(table.read<namedZero>()==3.f && table.read<0>()==3.f
           && table.read<NarrowPosition::First>()==3.f && table.read<FirstPosition>()==3.f
           && table.read<BoolPosition::First>()==3.f,
           "integer constants and signed/unscoped/bool enum positions remain compatible");
    expect(table.write<Position::Value>(11)==WriteResult::InvalidValue && source.writes==1,
           "named positions do not bypass limits");
    expect(table.read<Position::Reserved>().type()==ScalarType::Null
           && !table.read<Position::Reserved,double>()
           && table.write<Position::Reserved>(1)==WriteResult::ReadOnly,
           "named reserved position retains Scalar fallback");

    int calls=0;
    auto callback=[&calls](float value) noexcept {++calls;return value==3.f?CommandResult::Executed:CommandResult::Busy;};
    const CommandTable local{command("run",callback),reservedCommand()};
    expect(local.call<Position::Value>(3.f)==CommandResult::Executed && calls==1,
           "named command position retains native argument dispatch");
    expect(local.call<namedZero>(4.f)==CommandResult::Busy && local.call<FirstPosition>(3.f)==CommandResult::Executed
           && local.call<NarrowPosition::First>(3.f)==CommandResult::Executed && calls==4,
           "command integer and enum position spellings agree");
    expect(local.call<BoolPosition::Second>()==CommandResult::Unavailable && calls==4,
           "named reserved command cannot invoke a target");
}
}
int main() {
    checkNamedPositions();
    checkExplicitEnums();
    using ReadConversion=CallbackConversion<decltype(&freeRead),&freeRead>;
    using WriteConversion=CallbackConversion<decltype(&freeWrite),&freeWrite>;
    const FieldTable converted{
        field("converted","",ReadConversion{},WriteConversion{}),
        field("read","",ReadConversion{})};
    expect(converterCopies==0 && converterCalls==3,
           "factory converts callback wrappers once without internal copies");
    expect(converted.write<0>(8)==WriteResult::Busy && converted.read<1>()==8.f,
           "custom noexcept callback conversions retain exact targets");
    checkFunctions(std::make_index_sequence<5>{});
    matrix<float>();matrix<double>();matrix<bool>();
    matrix<std::uint8_t>();matrix<std::uint16_t>();matrix<std::uint32_t>();matrix<std::uint64_t>();
    matrix<std::int8_t>();matrix<std::int16_t>();matrix<std::int32_t>();matrix<std::int64_t>();
    matrix<char>();matrix<wchar_t>();matrix<long>();matrix<unsigned long long>();
    owner.value=15.f;
    expect(registry.read<makeId(1,0)>()==15.f && owner.reads==1,"reads invoke once and ignore write bounds");
    expect(registry.read<makeId(1,0),double>()==15.0 && owner.reads==2,"read conversion invokes once");
    expect(registry.write<makeId(1,0)>(11)==WriteResult::InvalidValue && owner.writes==0,"bounds checked before setter");
    expect(registry.write<makeId(1,0)>(5)==WriteResult::Applied && owner.writes==1,"global native setter invokes once");
    expect(oldBinding.read<makeId(1,0)>()==5.f,"static index compatibility");
    expect(registry.read<makeId(1,2)>().type()==ScalarType::Null
           && registry.write<makeId(1,2)>(Scalar::null())==WriteResult::ReadOnly,"reserved field keeps its position");
    expect(values.write<1>(1.9)==WriteResult::Applied && values.read<1>()==1,"enum exposes numeric type and truncates writes");
    expect(values.write<1>(3)==WriteResult::InvalidValue && modeOwner.writes==1,"enum bounds checked before cast");
    expect(values.write<3>(1999)==WriteResult::Applied && values.read<3>()==1999,"sparse enum gaps retain interval semantics");
    expect(values.write<3>(2001)==WriteResult::InvalidValue,"explicit sparse enum maximum");
    expect(values.write<4>(0)==WriteResult::Applied && values.write<4>(1000)==WriteResult::InvalidValue,"unfixed enum conversion guarded");
    int state=3,readCount=0;
    auto get=[&]() noexcept {++readCount;return state;};
    auto set=[&](int x) noexcept {state=x;return WriteResult::Applied;};
    const FieldTable borrowed{field("captured","",get,set)};
    expect(borrowed.write<0>(7.9)==WriteResult::Applied && borrowed.read<0>()==7
           && readCount==1,"borrowed closures use native path without lifetime extension");
    const auto defaults = limits(3);
    const FieldTable borrowedRead{field("read","",get,defaults)};
    expect(borrowedRead.read<0>()==7,"lvalue metadata is not mistaken for a setter");
    auto mutableGet=[n=0]() mutable noexcept { return ++n; };
    const auto constGet=[n=9]() noexcept { return n; };
    const FieldTable qualifiers{field("mutable","",mutableGet),field("const","",constGet)};
    expect(qualifiers.read<0>()==1 && qualifiers.read<0>()==2 && qualifiers.read<1>()==9,
           "native access retains mutable and const closure types");
    Derived derived;
    const Derived constant;
    const FieldTable inherited{
        field<&Base::read,&Base::write>("inherited","",derived),
        field<&Base::read>("constant","",constant)};
    expect(inherited.write<0>(8)==WriteResult::Applied && derived.padding==42
           && inherited.read<0>()==8.f && inherited.read<1>()==6.f,
           "native owner recovery preserves base adjustment and const qualification");
    Scalar scalarValue=Scalar::fromF64(12.7);
    auto scalarGet=[&]() noexcept {return scalarValue;};
    auto scalarSet=[&](const Scalar& s) noexcept {scalarValue=s;return WriteResult::Applied;};
    const FieldTable fallback{field("scalar","",ScalarType::U16,scalarGet,scalarSet),
        field(Field{"manual","",ScalarType::U16,[]() noexcept{return 12.7;}})};
    expect(fallback.read<0,double>()==12. && fallback.read<1,double>()==12.,"fallback honors declared type before requested type");
    expect(fallback.write<0>(9.8)==WriteResult::Applied && scalarValue.type()==ScalarType::U16
           && scalarValue.get<std::uint16_t>()==9,"Scalar callback fallback normalizes writes");
    Owner<float> second;
    const FieldTable runtime{field<&Owner<float>::read,&Owner<float>::write>("runtime","",second)};
    expect(runtime.write<0>(4)==WriteResult::Applied && second.value==4.f && owner.value==5.f,"runtime owner pointer comes from its descriptor");
    expect(commands.call<makeId(1,0)>(2.f,Mode::Auto)==CommandResult::Accepted,"global typed command routes to local table");
    expect(commands.call(makeId(1,0),3,1)==CommandResult::Accepted,"dynamic command convenience converts values");
    expect(prebuiltActions.call<0>(3.f,Mode::Auto)==CommandResult::Accepted
           && prebuiltActions.call<0>(11.f,Mode::Auto)==CommandResult::InvalidValue,
           "prebuilt command metadata is owned and enforced");
    auto capturedCommand=[&state](int value) noexcept {state=value;return CommandResult::Busy;};
    const CommandTable borrowedCommands{command("capture",capturedCommand)};
    const CommandCatalogTable borrowedRegistry{group("capture",borrowedCommands)};
    expect(borrowedRegistry.call<0>(25)==CommandResult::Busy && state==25,
           "global native command preserves borrowed closure and return value");
    second.value=std::numeric_limits<float>::infinity();
    expect(std::isinf(runtime.read<0>().value_or(0))
           && std::isinf(runtime.read<0,double>().value_or(0))
           && std::isinf(runtime[0].read<float>().value_or(0)) && !runtime.read<0,int>(),
           "native and dynamic reads preserve infinity only for floating destinations");
    second.value=std::numeric_limits<float>::quiet_NaN();
    expect(std::isnan(runtime.read<0>().value_or(0))
           && std::isnan(runtime[0].read<float>().value_or(0)) && !runtime.read<0,bool>(),
           "native and dynamic reads preserve NaN only for floating destinations");
    expect(commands.call<makeId(1,1)>(123)==CommandResult::Unavailable
           && commands.execute(makeId(1,1),nullptr,0)==CommandResult::Unavailable,"reserved command preserves ordinal");
    expect(commands.execute(makeId(2,0),nullptr,0)==CommandResult::NotFound,"command global bounds");
    char a[4096],b[4096];
    expect(writeSchema(registry.index(),a,sizeof a)>0 && writeSchema(rawGroups,2,b,sizeof b)>0
           && std::strcmp(a,b)==0,"table registry and explicit descriptor schema identical");
    expect(std::strstr(a,"\"id\":65537,\"n\":\"mode\"")!=nullptr,"schema computes group and entry IDs");
    expect(writeSchema(commands.index(),a,sizeof a)>0
           && std::strstr(a,"\"i\":1,\"id\":65537,\"n\":\"\",\"params\":[]")!=nullptr,
           "reserved command has an empty schema entry");
    std::printf("%d/%d table checks passed\n",checks-failures,checks);
    return failures?1:0;
}
