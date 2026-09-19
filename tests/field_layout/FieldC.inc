// Experiment C: immutable definition with a 32-byte-aligned numeric prefix.
struct alignas(32) Field {
    const Getter get;
    const Setter set;
    const ScalarType type;
    const std::uint8_t flags;
    const FieldId id;
    const char* const name;
    const char* const unit;
    const FieldType declaredType;

    constexpr Field(FieldId fieldId = 0, const char* fieldName = "",
                    const char* fieldUnit = "", FieldType fieldType = ScalarType::Null,
                    Getter getter = nullptr, Setter setter = nullptr) noexcept
        : get(getter), set(setter), type(static_cast<ScalarType>(fieldType)),
          flags(fieldType.restricted_ ? restrictedFlag_ : 0), id(fieldId),
          name(fieldName), unit(fieldUnit), declaredType(fieldType) {}

    constexpr Field(const Field&) noexcept = default;
    constexpr Field(Field&&) noexcept = default;
    // Const members and the declared constructors already disable assignment.
    // Keep assignment implicit: Clang 18 reports this class non-trivially
    // copyable when both assignment operators are explicitly declared deleted.
    // Contracts.cpp and ImmutableCompileFail.cpp verify the prohibition.

    // The generator inserts stable read/write methods here, changing only
    // their numeric tag source and the write-validation call.
    // @STABLE_METHODS@

private:
    static constexpr std::uint8_t restrictedFlag_ = 1;

    // Conversion has already succeeded. Unrestricted integers need no bounds
    // reads; unrestricted floats check finite native extrema from constants.
    // Restricted values use exactly one interval check, without a preceding
    // redundant finite check or reloading the cold type/restricted fields.
    TELEMETRY_FORCE_INLINE constexpr bool acceptsConverted_(const Scalar& value) const noexcept
    {
        if ((flags & restrictedFlag_) == 0) {
            if (type == ScalarType::F32) return detail::scalarFinite(value.get<float>());
            if (type == ScalarType::F64) return detail::scalarFinite(value.get<double>());
            return true;
        }
        switch (type) {
            case ScalarType::F32: return declaredType.contains_<float>(value);
            case ScalarType::F64: return declaredType.contains_<double>(value);
            case ScalarType::U8: return declaredType.contains_<std::uint8_t>(value);
            case ScalarType::U16: return declaredType.contains_<std::uint16_t>(value);
            case ScalarType::U32: return declaredType.contains_<std::uint32_t>(value);
            case ScalarType::U64: return declaredType.contains_<std::uint64_t>(value);
            case ScalarType::S8: return declaredType.contains_<std::int8_t>(value);
            case ScalarType::S16: return declaredType.contains_<std::int16_t>(value);
            case ScalarType::S32: return declaredType.contains_<std::int32_t>(value);
            case ScalarType::S64: return declaredType.contains_<std::int64_t>(value);
            case ScalarType::Bool: return declaredType.contains_<bool>(value);
            default: return false;
        }
    }
};
