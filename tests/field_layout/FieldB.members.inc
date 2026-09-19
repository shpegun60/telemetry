    // Experiment B: only member order changes; methods come from stable Field.
    Getter get = nullptr;
    Setter set = nullptr;
    FieldType declaredType = ScalarType::Null;
    FieldId id = 0;
    const char* name = "";
    const char* unit = "";

    constexpr Field(FieldId fieldId = 0, const char* fieldName = "",
                    const char* fieldUnit = "", FieldType fieldType = ScalarType::Null,
                    Getter getter = nullptr, Setter setter = nullptr) noexcept
        : get(getter), set(setter), declaredType(fieldType), id(fieldId),
          name(fieldName), unit(fieldUnit) {}
