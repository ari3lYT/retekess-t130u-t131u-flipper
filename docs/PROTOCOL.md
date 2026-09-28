# Protocol notes / Заметки о протоколе

## OBSERVED

- Approx. 250 kbit/s GFSK working receive profile on Flipper CC1101.
- CH1 823.475 MHz; CH17 831.475; CH18 863.475; CH19 863.975; CHS 864.475. These are working Flipper tune points, not confirmed T130U register settings.
- Channel markers: CH1 `0x9B1B9C1C`; CH17 `0x9B1BA424`; CH18 `0x9B1BA4A5`; CH19 `0x9B1BA525`; CHS `0x9B1BA5A6`.
- Bit positions: marker 0–31, fixed bit 32, G.711 A-law bits 33–528 (62 bytes), service field bits 529–544, CRC bits 545–560. CRC-16/AUG-CCITT: polynomial `0x1021`, initial `0x1D0F`, no reflection or final XOR; input bits 33–544.
- Service fields tested on the owner's T131U: ordinary `0x0000`; All Off `0x7777`; CHS target CH1 `0x1101`, CH18 `0x1112`, CH19 `0x1113`. CH16 `0x1110` appeared in a genuine transmitter capture but was not separately emulated.
- A-law audio generated for CH19 played on T131U. The new public encoder uses a synthetic frame template and awaits its own on-air test.

## INFERRED

- CHS target fields probably follow `0x1100 | channel`. The app can synthesize CH1–CH19 with correct CRC; only CH1/18/19 targets were validated with the earlier, private frame template.
- A repeated `10` preamble after the CRC gave stable receiver behavior in earlier CH19 audio tests. The exact over-the-air packet boundary remains unmeasured.

## UNKNOWN

- Pairing/group/transmitter identifiers; all channel frequencies and markers; exact hardware configuration and packet timing; independent MIC/AUX/Bluetooth controls; long audio streaming; intelligible playback on Flipper's own speaker.

Raw captures containing personal speech are intentionally withheld. Source scripts remain as a record of the methods and can be run on fresh captures from the reader's own hardware.
