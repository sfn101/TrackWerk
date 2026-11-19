#!/usr/bin/env bash
# Builds qjs.exe and a minimal ffmpeg.exe for Studio SOURCE on Windows (specs/windows-sources.md).
# Run by fetch-helpers.ps1 under MSYS2 UCRT64 (gcc, make, diffutils). Both are static: no DLLs.
#   build-helpers.sh <quickjs .tar.xz> <ffmpeg .tar.xz> <work dir> <output dir>
set -euo pipefail
QJS_TAR=$1 FF_TAR=$2 WORK=$3 OUT=$4
mkdir -p "$WORK" "$OUT"
JOBS=$(nproc)

# ── QuickJS (MIT): the JS engine yt-dlp needs for YouTube; the same version as the Linux build ──
if [ ! -f "$OUT/qjs.exe" ]; then
    rm -rf "$WORK/quickjs-2026-06-04"
    tar -xJf "$QJS_TAR" -C "$WORK"
    SRC="$WORK/quickjs-2026-06-04"
    FLAGS=(-O2 -D_GNU_SOURCE -D__USE_MINGW_ANSI_STDIO -DCONFIG_VERSION='"2026-06-04"' -I"$SRC" -funsigned-char)
    COMMON=("$SRC/quickjs.c" "$SRC/dtoa.c" "$SRC/libregexp.c" "$SRC/libunicode.c" "$SRC/cutils.c" "$SRC/quickjs-libc.c")
    gcc "${FLAGS[@]}" "$SRC/qjsc.c" "${COMMON[@]}" -static -lm -lpthread -o "$WORK/qjsc.exe"
    (cd "$SRC" && "$WORK/qjsc.exe" -c -o "$WORK/repl.c" -m repl.js)
    gcc "${FLAGS[@]}" "$SRC/qjs.c" "$WORK/repl.c" "${COMMON[@]}" -static -s -lm -lpthread -o "$OUT/qjs.exe"
fi

# ── FFmpeg (LGPL-2.1+): only what SOURCE asks of it — copy the audio of a fragmented MP4 into a plain
#    one (mov demuxer → mp4/ipod muxer, no re-encoding). Nothing else is enabled.
if [ ! -f "$OUT/ffmpeg.exe" ]; then
    rm -rf "$WORK/ffmpeg-7.1" "$WORK/ffmpeg-build"
    tar -xJf "$FF_TAR" -C "$WORK"
    mkdir -p "$WORK/ffmpeg-build"
    cd "$WORK/ffmpeg-build"
    ../ffmpeg-7.1/configure \
        --disable-everything --disable-autodetect --disable-network --disable-doc --disable-debug \
        --disable-x86asm --disable-ffplay --disable-ffprobe --disable-avdevice --disable-swscale \
        --disable-postproc --enable-ffmpeg --enable-static --disable-shared --enable-small \
        --enable-protocol=file,pipe --enable-demuxer=mov --enable-muxer=mp4,ipod,mov \
        --enable-decoder=aac --enable-parser=aac \
        --extra-ldflags=-static
    make -j"$JOBS" ffmpeg.exe
    strip -o "$OUT/ffmpeg.exe" ffmpeg.exe
fi

# The licence texts of exactly the sources built above (the packaging script ships them).
mkdir -p "$OUT/licenses"
[ -d "$WORK/quickjs-2026-06-04" ] || tar -xJf "$QJS_TAR" -C "$WORK"
[ -d "$WORK/ffmpeg-7.1" ] || tar -xJf "$FF_TAR" -C "$WORK"
cp "$WORK/quickjs-2026-06-04/LICENSE" "$OUT/licenses/QuickJS-LICENSE.txt"
cp "$WORK/ffmpeg-7.1/COPYING.LGPLv2.1" "$OUT/licenses/FFmpeg-COPYING.LGPLv2.1.txt"
cp "$WORK/ffmpeg-7.1/LICENSE.md" "$OUT/licenses/FFmpeg-LICENSE.md"

ls -l "$OUT" "$OUT/licenses"
