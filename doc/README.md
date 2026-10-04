# Документація Telemetry

Для інтеграції почніть із [посібника користувача](user/README.md).
Він описує наявні API й містить повні програми, що збираються в CI.

| Документ | Призначення |
| --- | --- |
| [API шпаргалка](user/API-CHEATSHEET.md) | Швидко згадати декларації, чотири рівні API та files |
| [Native API](user/NativeApi.md) | Декларації, типи, callback forms, slots, доступ та iteration |
| [Транспорт і ресурси](user/TransportAndResources.md) | Custom files, framing, protocol, прямий encoded доступ |
| [Borrowed native values](BorrowedNativeValues.md) | Контракт `const T&`, lifetime, zero-copy outputs |
| [Migration guide](StructuredTelemetryV3MigrationGuide.md) | Перехід зі старого Scalar API на поточний |
| [Implementation plan](StructuredTelemetryV3ImplementationPlan.md) | Поетапна специфікація та архітектурні рішення |
| [Freeze qualification](StructuredTelemetryV3FreezeQualification.md) | Зафіксовані source/wire contracts та acceptance ceilings |
| [Final qualification](StructuredTelemetryV3FinalQualification.md) | Докази, межі перевірок і статус апаратної кваліфікації |
| [Test index](../tests/README.md) | Підтримувані runners і відмінність виконання від компіляції |
| [Repository maintenance](RepositoryMaintenance.md) | Структура, прибирання та збереження локальних artifacts |

## Історія та review

[Review 2026-09-25](Review-2026-09-25.md), його
[матеріали](Review-2026-09-25/) і [архів вимірювань](evidence/pre-unification/README.md)
залишені з оригінальними source identities. Вони описують конкретні старі
ревізії; їхні шляхи, API та числа не слід трактувати як довідник поточної
бібліотеки. Транскрипти не переписуються заднім числом.

[ScalarMetadataStorageRefactorPlan.md](ScalarMetadataStorageRefactorPlan.md)
є історичним планом для попередньої моделі, а не завданням додати limits або
defaults у нинішній structured API.
