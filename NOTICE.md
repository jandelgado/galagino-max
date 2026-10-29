## License

The galagino source code is GPLv3 ([LICENSE](LICENSE)). Not everything in this repo is covered by it:

* **Third-party code** keeps its own license:

  | Code | License |
  | --- | --- |
  | Ports of MAME code (sound chips, DVG/AVG, mathbox, MB88xx, CTC, RIOT, ...) | BSD-3-Clause or GPL-2.0+, as in MAME |
  | `ym2203fm` (ymfm, Aaron Giles) | BSD-3-Clause |
  | `sid` (Rockbox TinySID) | GPL-2.0+ |
  | `cpus/i8048` (derived from [asig/kosmos-cp1](https://github.com/asig/kosmos-cp1)) | GPL-3.0 |
  | `hal/rp2350` DVI/HDMI encoding (derived from [PicoPlus-devel/pico-galagino](https://github.com/PicoPlus-devel/pico-galagino)) | GPL-3.0 |
  | `hal/rp2350` HSTX DVI setup, `ports/rp2350/dvi_test` (based on [pico-examples](https://github.com/raspberrypi/pico-examples) `dvi_out_hstx_encoder`, Raspberry Pi) | BSD-3-Clause, [notice](source/src/hal/rp2350/LICENSE-pico-examples) |
  | [Bluepad32](https://github.com/ricardoquesada/bluepad32) (Pico 2 W pad, fetched at build time, not in this repo) | Apache-2.0 |
  | BTstack (Pico 2 W pad, part of the pico-sdk, not in this repo) | free on Raspberry Pi chips (pico-sdk `src/rp2_common/pico_btstack/LICENSE.RP`) |
  | `cpus/i8086` (FabGL, 8086tiny) | MIT, [notice](source/src/cpus/i8086/LICENSE-8086tiny) |
  | Marble Madness logic ([Marble Love](https://github.com/magno73/marble-love)) | MIT, [notice](source/src/machines/marble/LICENSE-marble-love) |
  | `audio/codec_es8311` register setup (Espressif es8311 driver) | Apache-2.0 |
  | `emulation/lzma` (LZMA SDK) | public domain |
  | `NintendoExtensionCtrl` (nunchuck, PlatformIO dependency) | LGPLv3 |

* **Z80 core** (Marat Fayzullin): not in this repo. romconv unpacks and patches it from
  `Z80-081707.zip` (see [romszip](romszip/README.md)). Its license forbids commercial distribution,
  which GPLv3 does not allow. Build the firmware yourself. Do not distribute firmware binaries.
* **Logos, screenshots and samples** (`logos/`, `images/`, `samples/`) are the property of the game
  manufacturers and the sample authors, and are not licensed by this repo.
* **ROMs** are not included. Bring your own.

