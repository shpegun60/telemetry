# A/B/C/A32/B32/B64 ARM object measurements

arm-none-eabi-g++.exe (GNU Tools for STM32 14.3.rel1.20251027-0700) 14.3.1 20250623

No board execution or cycle measurements. Sizes below are bytes.

| Variant | sizeof | alignof | get | set | type | flags | declaredType | id | name | unit |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A | 80 | 8 | 56 | 68 | absent | absent | 16 | 0 | 4 | 8 |
| B | 80 | 8 | 0 | 12 | absent | absent | 24 | 64 | 68 | 72 |
| C | 96 | 32 | 0 | 12 | 20 | 21 | 40 | 24 | 28 | 32 |
| A32 | 96 | 32 | 56 | 68 | absent | absent | 16 | 0 | 4 | 8 |
| B32 | 96 | 32 | 0 | 12 | absent | absent | 24 | 64 | 68 | 72 |
| B64 | 128 | 64 | 0 | 12 | absent | absent | 24 | 64 | 68 | 72 |

## Object sections

| Variant | Optimization | Object | .text | .rodata | .data | .bss |
|---|---|---|---:|---:|---:|---:|
| A | O2 | Probe | 12112 | 82177 | 0 | 0 |
| A | O2 | Json | 10352 | 552 | 0 | 0 |
| A | Os | Probe | 11902 | 82158 | 0 | 0 |
| A | Os | Json | 6966 | 524 | 0 | 0 |
| B | O2 | Probe | 12128 | 82177 | 0 | 0 |
| B | O2 | Json | 10352 | 552 | 0 | 0 |
| B | Os | Probe | 11910 | 82158 | 0 | 0 |
| B | Os | Json | 6958 | 524 | 0 | 0 |
| C | O2 | Probe | 12368 | 98569 | 0 | 0 |
| C | O2 | Json | 10352 | 552 | 0 | 0 |
| C | Os | Probe | 11936 | 98550 | 0 | 0 |
| C | Os | Json | 6958 | 524 | 0 | 0 |
| A32 | O2 | Probe | 12112 | 98569 | 0 | 0 |
| A32 | O2 | Json | 10352 | 552 | 0 | 0 |
| A32 | Os | Probe | 11902 | 98550 | 0 | 0 |
| A32 | Os | Json | 6966 | 524 | 0 | 0 |
| B32 | O2 | Probe | 12128 | 98569 | 0 | 0 |
| B32 | O2 | Json | 10352 | 552 | 0 | 0 |
| B32 | Os | Probe | 11910 | 98550 | 0 | 0 |
| B32 | Os | Json | 6958 | 524 | 0 | 0 |
| B64 | O2 | Probe | 12104 | 131369 | 0 | 0 |
| B64 | O2 | Json | 10312 | 552 | 0 | 0 |
| B64 | Os | Probe | 11882 | 131350 | 0 | 0 |
| B64 | Os | Json | 6950 | 524 | 0 | 0 |

## O2 probe bodies

Cells show bytes / static instruction count. Function bytes include literal pools; instruction counts exclude them.
Counts cover all branches, not the instructions executed by one call, and are not cycle counts.
Aliases may have no separate disassembly count. Helper functions are included in object .text totals above.

| Function | A | B | C | A32 | B32 | B64 |
|---|---:|---:|---:|---:|---:|---:|
| layout_enum_read | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 |
| layout_enum_write | 36 / 15 | 36 / 15 | 36 / 15 | 36 / 15 | 36 / 15 | 36 / 15 |
| layout_find_runtime | 40 / 16 | 40 / 16 | 40 / 16 | 40 / 16 | 40 / 16 | 36 / 15 |
| layout_plain_read | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 |
| layout_plain_write | 36 / 15 | 36 / 15 | 36 / 15 | 36 / 15 | 36 / 15 | 36 / 15 |
| layout_read_fixed | 2412 / 563 | 2408 / 563 | 2408 / 563 | 2412 / 563 | 2408 / 563 | 2408 / 563 |
| layout_read_known | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 |
| layout_read_known_bounded | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 |
| layout_read_normalized | 60 / 17 | 64 / 18 | 60 / 17 | 60 / 17 | 64 / 18 | 64 / 18 |
| layout_read_runtime | 2428 / 571 | 2428 / 572 | 2428 / 572 | 2428 / 571 | 2428 / 572 | 2424 / 571 |
| layout_read_runtime_float | 2436 / 541 | 2428 / 540 | 2428 / 540 | 2436 / 541 | 2428 / 540 | 2428 / 540 |
| layout_write_fixed_float | 1028 / 300 | 1032 / 299 | 1132 / 325 | 1028 / 300 | 1032 / 299 | 1028 / 298 |
| layout_write_fixed_u16 | 652 / 227 | 660 / 227 | 676 / 234 | 652 / 227 | 660 / 227 | 656 / 226 |
| layout_write_full_u16 | 28 / 11 | 28 / 11 | 28 / 11 | 28 / 11 | 28 / 11 | 28 / 11 |
| layout_write_known | 68 / 21 | 68 / 21 | 68 / 21 | 68 / 21 | 68 / 21 | 68 / 21 |
| layout_write_known_constant | 32 / 12 | 32 / 12 | 32 / 12 | 32 / 12 | 32 / 12 | 32 / 12 |
| layout_write_readonly | 4 / 2 | 4 / 2 | 4 / 2 | 4 / 2 | 4 / 2 | 4 / 2 |
| layout_write_restricted_f32 | 64 / 21 | 64 / 21 | 64 / 21 | 64 / 21 | 64 / 21 | 64 / 21 |
| layout_write_restricted_u16 | 40 / 16 | 40 / 16 | 40 / 16 | 40 / 16 | 40 / 16 | 40 / 16 |
| layout_write_runtime_float | 1044 / 310 | 1052 / 310 | 1160 / 334 | 1044 / 310 | 1052 / 310 | 1048 / 309 |
| layout_write_runtime_u16 | 644 / 228 | 648 / 228 | 666 / 234 | 644 / 228 | 648 / 228 | 644 / 227 |

## Os probe bodies

Cells show bytes / static instruction count. Function bytes include literal pools; instruction counts exclude them.
Counts cover all branches, not the instructions executed by one call, and are not cycle counts.
Aliases may have no separate disassembly count. Helper functions are included in object .text totals above.

| Function | A | B | C | A32 | B32 | B64 |
|---|---:|---:|---:|---:|---:|---:|
| layout_enum_read | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 |
| layout_enum_write | 56 / 20 | 56 / 20 | 56 / 20 | 56 / 20 | 56 / 20 | 56 / 20 |
| layout_find_runtime | 38 / 16 | 38 / 16 | 38 / 16 | 38 / 16 | 38 / 16 | 36 / 15 |
| layout_plain_read | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 |
| layout_plain_write | 56 / 20 | 56 / 20 | 56 / 20 | 56 / 20 | 56 / 20 | 56 / 20 |
| layout_read_fixed | 2242 / 501 | 2242 / 502 | 2208 / 504 | 2242 / 501 | 2242 / 502 | 2234 / 499 |
| layout_read_known | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 |
| layout_read_known_bounded | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 | 8 / 3 |
| layout_read_normalized | 60 / 17 | 60 / 17 | 64 / 18 | 60 / 17 | 60 / 17 | 60 / 17 |
| layout_read_runtime | 2252 / 507 | 2252 / 508 | 2218 / 510 | 2252 / 507 | 2252 / 508 | 2246 / 506 |
| layout_read_runtime_float | 2302 / 520 | 2294 / 519 | 2288 / 526 | 2302 / 520 | 2294 / 519 | 2294 / 519 |
| layout_write_fixed_float | 1078 / 310 | 1082 / 310 | 1112 / 320 | 1078 / 310 | 1082 / 310 | 1078 / 308 |
| layout_write_fixed_u16 | 686 / 235 | 690 / 235 | 742 / 249 | 686 / 235 | 690 / 235 | 690 / 235 |
| layout_write_full_u16 | 30 / 10 | 30 / 10 | 28 / 9 | 30 / 10 | 30 / 10 | 30 / 10 |
| layout_write_known | 84 / 25 | 84 / 25 | 40 / 14 | 84 / 25 | 84 / 25 | 84 / 25 |
| layout_write_known_constant | 88 / 25 | 88 / 25 | 48 / 15 | 88 / 25 | 88 / 25 | 88 / 25 |
| layout_write_readonly | 4 / 2 | 4 / 2 | 4 / 2 | 4 / 2 | 4 / 2 | 4 / 2 |
| layout_write_restricted_f32 | 84 / 25 | 84 / 25 | 64 / 19 | 84 / 25 | 84 / 25 | 84 / 25 |
| layout_write_restricted_u16 | 56 / 20 | 56 / 20 | 58 / 21 | 56 / 20 | 56 / 20 | 56 / 20 |
| layout_write_runtime_float | 1080 / 318 | 1084 / 318 | 1118 / 329 | 1080 / 318 | 1084 / 318 | 1080 / 316 |
| layout_write_runtime_u16 | 704 / 243 | 708 / 243 | 758 / 256 | 704 / 243 | 708 / 243 | 704 / 241 |
