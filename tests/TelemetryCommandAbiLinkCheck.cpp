#include "serialization/TelemetryCommandJson.h"
int main()
{
    char text[64];
    return telemetry::writeSchema(telemetry::CommandIndex{},text,sizeof(text)) != 0 ? 0 : 1;
}
