# License provenance

Application files in this directory are MIT, consistent with Telemetry.
The pin, clock and GPDMA configuration follows the existing local
`COBS/stm32_cube_test/h7s_cobs_test` CubeMX project and the ST
NUCLEO-H7S3L8 UART examples. No generated project or vendor source is copied.

`Build.ps1` consumes STM32CubeH7RS HAL, CMSIS device/system and startup sources
from `STM32_CUBE_ROOT`. Their STMicroelectronics/Arm copyright notices and
license files in that SDK remain authoritative. The receipt records their
exact input hashes. The SDK is neither vendored nor relicensed here.

The external COBS UART driver, COBS codec, fixed pools, spsc and tiny_delegate
retain the MIT licenses in their respective checkouts. Their source files are
compiled in place without modification; retain those license notices when
redistributing a combined image/source package.
