// Review probe (catalog-json): compile-only check of the README's ARM32 layout
// statements (lib/telemetry/README.md "Field ABI migration and storage").
#include "Telemetry.h"
using namespace telemetry;
static_assert(sizeof(Field) == 96 && alignof(Field) == 32, "Field 96/32");
static_assert(offsetof(Field, get) == 0 && offsetof(Field, readType) == 8, "get/readType 0/8");
static_assert(offsetof(Field, name) == 12 && offsetof(Field, unit) == 16, "name/unit 12/16");
static_assert(Field::abiFlagsOffset() == 20, "flags 20");
static_assert(offsetof(Field, set) == 32 && offsetof(Field, declaredType) == 40, "set/type 32/40");
static_assert(sizeof(Catalog) == 12 && sizeof(CommandCatalog) == 12, "Catalog/CommandCatalog 12");
static_assert(sizeof(Command) == 20, "Command 20");
static_assert(sizeof(FieldType) == 48 && sizeof(Scalar) == 16, "FieldType 48, Scalar 16");
static_assert(sizeof(Getter) == 8 && sizeof(Setter) == 8, "Getter/Setter 8");
static_assert(sizeof(CatalogIndex) == 8, "CatalogIndex 8");
static_assert(sizeof(Field[20]) == 1920, "Field[20] 1920");
