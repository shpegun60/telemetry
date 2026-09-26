// Review probe (fields slice): offsetof on these types is only conditionally
// supported when they are not standard-layout. Report their status.
#include "Telemetry.h"
#include <cstdio>

int main()
{
    std::printf("standard layout: Scalar=%d FieldType=%d Field=%d Getter=%d Setter=%d\n",
                int(std::is_standard_layout_v<telemetry::Scalar>),
                int(std::is_standard_layout_v<telemetry::FieldType>),
                int(std::is_standard_layout_v<telemetry::Field>),
                int(std::is_standard_layout_v<telemetry::Getter>),
                int(std::is_standard_layout_v<telemetry::Setter>));
}
