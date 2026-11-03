# Optional Qt SerialPort transport; COBS sources belong to this application. MIT.
isEmpty(TELEMETRY_QT_SERIAL_TRANSPORT_PRI_INCLUDED) {
    TELEMETRY_QT_SERIAL_TRANSPORT_PRI_INCLUDED = 1
    include($$PWD/qt_transport.pri)
    QT += serialport
    !exists($$COBS_ROOT/src/cobs/Cobs.h):error("Set COBS_ROOT to the COBS source checkout")
    INCLUDEPATH += $$COBS_ROOT/src $$PWD/../../delegate
    HEADERS *= $$PWD/SerialTransport.hpp $$PWD/../serial/Profile.hpp
    SOURCES *= $$PWD/SerialTransport.cpp $$COBS_ROOT/src/cobs/Encoder.cpp $$COBS_ROOT/src/cobs/Decoder.cpp
}
