# Shared serial profile

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[`Profile.hpp`](Profile.hpp) selects the existing bounded COBS/CRC16 framing
and absolute baud-aware budget shared by the Qt UART transport, NUCLEO firmware
and host checks. There is one profile implementation and no Qt dependency here.

This implementation header requires the external COBS checkout and is not part
of the installed Qt public headers. Applications using the installed transport
query its limits through `SerialTransport`, without including COBS. See
[Qt transport](../../../doc/QtTransport.md).
