# Optional snapshot-preserving Workbench reads on its existing client FIFO. MIT.
isEmpty(TELEMETRY_READ_MANY_WORKBENCH_PRI_INCLUDED) {
    TELEMETRY_READ_MANY_WORKBENCH_PRI_INCLUDED = 1
    include($$PWD/read_many.pri)
    include($$PWD/../workbench/workbench.pri)
    HEADERS += $$PWD/Workbench.hpp
}
