# Optional complete-frame Mixed Batch executor; late opt-in is supported. MIT.
include($$PWD/../../telemetry/telemetry.pri)
include($$PWD/../../wire/wire.pri)
HEADERS *= $$clean_path($$PWD/../../telemetry/encoded/Batch.hpp) \
    $$clean_path($$PWD/../encoded/BatchDispatcher.hpp) $$clean_path($$PWD/../encoded/Dispatcher.hpp)
SOURCES *= $$clean_path($$PWD/../../telemetry/encoded/Batch.cpp) \
    $$clean_path($$PWD/../encoded/BatchDispatcher.cpp) $$clean_path($$PWD/../encoded/Dispatcher.cpp)
