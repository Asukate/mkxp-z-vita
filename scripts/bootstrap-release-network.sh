#!/usr/bin/env bash
# Build the launcher-only HTTPS client with the VitaSDK Mbed TLS backend.
set -euo pipefail
WS="${1:?usage: bootstrap-release-network.sh WORKSPACE [JOBS]}"
JOBS="${2:-4}"
VITASDK="${VITASDK:?set VITASDK}"
AREA="$WS/release-network"
PREFIX="$AREA/prefix"
mkdir -p "$AREA"
[[ "$(pkg-config --modversion "$VITASDK/arm-vita-eabi/lib/pkgconfig/mbedtls.pc")" == 3.6.5 ]] || { echo 'release checks require VitaSDK Mbed TLS 3.6.5' >&2; exit 1; }
RECEIPT="$AREA/inputs.sha256"
TEMP="$AREA/inputs.new"
sha256sum "$VITASDK"/arm-vita-eabi/lib/libmbed{tls,x509,crypto}.a "$0" > "$TEMP"
if [[ -f "$PREFIX/lib/libcurl.a" && -f "$RECEIPT" ]] && cmp -s "$RECEIPT" "$TEMP"; then rm "$TEMP"; exit 0; fi
ARCHIVE="$AREA/curl-8.17.0.tar.gz"
if [[ ! -f "$ARCHIVE" ]]; then
    wget -q 'https://github.com/curl/curl/releases/download/curl-8_17_0/curl-8.17.0.tar.gz' -O "$ARCHIVE"
fi
printf '%s  %s\n' e8e74cdeefe5fb78b3ae6e90cd542babf788fa9480029cfcee6fd9ced42b7910 "$ARCHIVE" | sha256sum -c -
if [[ ! -d "$AREA/curl-8.17.0" ]]; then tar -xzf "$ARCHIVE" -C "$AREA"; fi
cmake -S "$AREA/curl-8.17.0" -B "$AREA/build" \
    -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_CURL_EXE=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
    -DENABLE_IPV6=OFF -DCURL_DISABLE_SOCKETPAIR=ON -DHAVE_FCNTL_O_NONBLOCK=OFF \
    -DENABLE_THREADED_RESOLVER=OFF -DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF \
    -DENABLE_CURL_MANUAL=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_OPENSSL=OFF \
    -DCURL_USE_MBEDTLS=ON -DCURL_ZLIB=OFF -DCURL_ZSTD=OFF -DCURL_BROTLI=OFF \
    -DHTTP_ONLY=ON -DCURL_CA_BUNDLE=app0:/ca-certificates.crt
# Vita's libc has no pipe2 despite the cross-compiler feature probe.
sed -i '/HAVE_PIPE2/d' "$AREA/build/lib/curl_config.h"
cmake --build "$AREA/build" -j "$JOBS"
cmake --install "$AREA/build"
mv "$TEMP" "$RECEIPT"
