# libopus 1.5.2 in Osplay

Source: https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz
(SHA-256 `65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1`, checked against
xiph's `SHA256SUMS.txt` on 2026-11-20). License: `COPYING` (BSD-3-Clause style).

Only the float build of the codec is kept, unchanged:

- `include/`: `opus.h`, `opus_defines.h`, `opus_types.h`, `opus_custom.h`
- `celt/`: the files of `CELT_SOURCES` in `celt_sources.mk`, and every `celt/*.h`
- `silk/`: `SILK_SOURCES` and every `silk/*.h`; `silk/float/`: `SILK_SOURCES_FLOAT` and its headers
- `src/`: `opus.c`, `opus_decoder.c`, `opus_encoder.c`, `extensions.c`, `repacketizer.c`,
  `analysis.c`, `mlp.c`, `mlp_data.c`, and every `src/*.h` except `mapping_matrix.h`

Left out: the DNN, DRED and OSCE machine-learning parts (`dnn/`, 17 MB of model data), the
hand-written SIMD and assembly (`arm/`, `x86/`, `mips/`), the fixed-point build, multistream and
ambisonics, tests and demos. Osplay compiles it with `OPUS_BUILD` and `USE_ALLOCA` (plus
`HAVE_LRINT` and `HAVE_LRINTF` outside MSVC) in `core/CMakeLists.txt` and `Package.swift`.

To update: download the new release, check its checksum, and copy the same lists again. The
Apple apps ship `COPYING` as `macos/Shared/Licenses/Opus-LICENSE.txt` and the Android app as
`android/app/src/main/assets/licenses/Opus-LICENSE.txt` (`scripts/lint.sh` checks both copies).
