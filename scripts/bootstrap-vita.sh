#!/usr/bin/env bash
# mkxp-z-vita dependency bootstrap.
#
# Usage:
#   VITASDK=/opt/vitasdk ./scripts/bootstrap-vita.sh [--ws <dir>] [--profile real-vita|vita3k] [-j N]
#
# What it does:
#   1. validates toolchain + host prerequisites (fails loudly)
#   2. fetches pinned public dependencies and uses bundled source snapshots
#   3. builds + installs them into <ws>/prefix (without modifying $VITASDK)
#   4. writes <ws>/bootstrap-manifest.txt with SHAs and versions
#
# Ruby's deleted upstream branch is preserved under deps/vendor/.
# The locally authored SDL shim is bundled as source.
set -euo pipefail

WS=""
PROFILE="real-vita"
JOBS="4"
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) printf 'usage: %s [--ws DIR] [--profile real-vita|vita3k] [-j N]\n' "$0"; exit 0;;
        --ws|--profile|-j|--out|--title-id)
            [[ $# -ge 2 && "$2" != --* && -n "$2" ]] || { printf 'missing value for %s\n' "$1" >&2; exit 2; } ;;
    esac
    case "$1" in
        --ws) WS="$2"; shift 2;;
        --profile) PROFILE="$2"; shift 2;;
        -j) JOBS="$2"; shift 2;;
        *) printf 'usage: %s [--ws DIR] [--profile real-vita|vita3k] [-j N]\n' "$0" >&2; exit 2;;
    esac
done

[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || { echo 'jobs must be a positive integer' >&2; exit 2; }
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEPS_DIR="$ROOT/deps"
CAND_SRC="$ROOT"
SHIM_SRC="$DEPS_DIR/vendor/vita-sdl2-shim"
source "$DEPS_DIR/versions.lock"
if [[ -z "$WS" ]]; then WS="$ROOT/build/$PROFILE"; fi
WS="$(realpath -m "$WS")"

VITASDK="${VITASDK:-}"
[[ -n "$VITASDK" ]] || { printf 'ERROR: VITASDK must be set (external prerequisite)\n' >&2; exit 1; }
VITABIN="$VITASDK/bin"
for t in arm-vita-eabi-gcc arm-vita-eabi-g++ arm-vita-eabi-ar arm-vita-eabi-ranlib; do
    [[ -x "$VITABIN/$t" ]] || { printf 'ERROR: missing %s\n' "$VITABIN/$t" >&2; exit 1; }
done
for t in cmake meson ninja pkg-config git python3 zstd autoconf automake libtool make wget xxd; do
    command -v "$t" >/dev/null || { printf 'ERROR: missing host tool %s\n' "$t" >&2; exit 1; }
done
[[ -x /usr/bin/ruby ]] || { printf 'ERROR: missing baseruby /usr/bin/ruby\n' >&2; exit 1; }
[[ "$PROFILE" == real-vita || "$PROFILE" == vita3k ]] || { printf 'ERROR: profile must be real-vita|vita3k\n' >&2; exit 1; }

SRC="$WS/src"; PREFIX="$WS/prefix"; BUILD="$WS/build"
PKGDIR="$PREFIX/lib/pkgconfig"
mkdir -p "$SRC" "$PREFIX" "$BUILD" "$PKGDIR"
export PKG_CONFIG_LIBDIR="$PKGDIR"
export PKG_CONFIG=/usr/bin/pkg-config
unset PKG_CONFIG_PATH
export PATH="$VITABIN:$PATH"
ARCH_FLAGS="-marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard"
# Same OS defines as linux/meson-vita.txt. The Vita compiler only predefines
# __vita__; POSIX-portable deps (physfs et al.) need -D__linux__ to select
# their Unix backend. Ruby keeps its proven flag set (no __linux__) below.
TP_DEFS="-D__linux__ -D__linux -D__vita__ -D__psp2__"  # both spellings: GCC predefines __linux AND __linux__ on Linux; physfs tests __linux
TP_CFLAGS="$ARCH_FLAGS -O2 $TP_DEFS -I$PREFIX/include"  # -I prefix: workspace compat headers (endian.h) + built deps

log() { printf '=== BOOTSTRAP %s ===\n' "$*"; }
sha_check() { # sha_check <file> <expected>
    echo "$2  $1" | sha256sum -c - || { printf 'ERROR: SHA mismatch %s\n' "$1" >&2; exit 1; }; }
git_fetch_pin() { # git_fetch_pin <url> <sha> <dest>
    local url="$1" sha="$2" dest="$3"
    if [[ ! -d "$dest/.git" ]]; then git init -q "$dest"; fi
    git -C "$dest" fetch -q "$url" "$sha" || { printf 'ERROR: cannot fetch %s from %s\n' "$sha" "$url" >&2; exit 1; }
    local got; got="$(git -C "$dest" rev-parse FETCH_HEAD)"
    [[ "$got" == "$sha" ]] || { printf 'ERROR: fetched %s != %s\n' "$got" "$sha" >&2; exit 1; }
    git -C "$dest" checkout -q "$sha" 2>/dev/null || git -C "$dest" checkout -q FETCH_HEAD
    git -C "$dest" rev-parse HEAD
}

sha_check "$DEPS_DIR/ruby-vita-a2d396e-clean.patch" "$RUBY_PATCH_SHA256"
sha_check "$DEPS_DIR/patches/vitaGL/7f16d15c-vita-mkxp.patch" "$VITAGL_PATCH_SHA256"
python3 "$SCRIPT_DIR/build-state.py" dependencies "$WS" "$PROFILE"
MANIFEST="$WS/bootstrap-manifest.txt"
: > "$MANIFEST"
rec() { printf '%s\n' "$*" >> "$MANIFEST"; }
rec "profile=$PROFILE"
rec "dependency-lock=$(sha256sum "$DEPS_DIR/versions.lock" | cut -d' ' -f1)"
# Locally authored Vita compatibility headers (no upstream): endian.h maps
# newlib machine/endian to glibc-style names; al.h/alc.h/alext.h declare the
# minimal OpenAL surface openal-vita.c implements; encoding.h stubs
# ruby/encoding.h for the Vita target. Authorship and terms are recorded in
# the file headers and THIRD-PARTY-NOTICES.md.
for compat_hdr in endian.h al.h alc.h alext.h encoding.h; do
    install -Dm644 "$DEPS_DIR/vita-compat/$compat_hdr" "$PREFIX/include/$compat_hdr"
done
install -Dm644 "$DEPS_DIR/vita-compat/sys/mman.h" "$PREFIX/include/sys/mman.h"
rec "vita-compat-headers=$(cd "$DEPS_DIR/vita-compat" && sha256sum endian.h al.h alc.h alext.h encoding.h sys/mman.h | sha256sum | cut -d' ' -f1)"
rec "vitasdk=$VITASDK"
rec "vitasdk_gcc=$("$VITABIN/arm-vita-eabi-gcc" --version | head -1)"
rec "cmake=$(cmake --version | head -1) meson=$(meson --version) ninja=$(ninja --version) baseruby=$(/usr/bin/ruby --version)"

# ---------------------------------------------------------------- third-party
log "third-party libraries into $PREFIX"
TP="$SRC/thirdparty"; mkdir -p "$TP"
# All upstream revisions are read from deps/versions.lock.
build_autotools() { # $1=name $2=srcdir [$3... extra-configure-args]
    local name="$1" dir="$2"; shift 2
    log "$name configure+build"
    if [[ ! -x "$dir/configure" && -x "$dir/autogen.sh" ]]; then
        (cd "$dir" && ./autogen.sh >/dev/null 2>&1 || true)
    fi
    if [[ -x "$dir/configure" ]]; then
        (cd "$dir" && \
            ./configure --host=arm-vita-eabi --prefix="$PREFIX" \
                --enable-static=yes --enable-shared=no "$@" \
                CC="$VITABIN/arm-vita-eabi-gcc" AR="$VITABIN/arm-vita-eabi-ar" \
                CFLAGS="$ARCH_FLAGS -O2" \
                CPPFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib" >/dev/null && \
            make -j"$JOBS" >/dev/null && make install >/dev/null)
    else
        log "$name: no autotools, trying meson fallback"
        rm -rf "$dir/wpbuild" && mkdir -p "$dir/wpbuild"
        (cd "$dir/wpbuild" && meson setup .. --cross-file <(printf "[binaries]\nc = '%s/arm-vita-eabi-gcc'\ncpp = '%s/arm-vita-eabi-g++'\nar = '%s/arm-vita-eabi-ar'\nstrip = '%s/arm-vita-eabi-strip'\n[host_machine]\nsystem = 'vita'\ncpu_family = 'arm'\ncpu = 'cortex-a9'\nendian = 'little'\n" "$VITABIN" "$VITABIN" "$VITABIN" "$VITABIN") \
            --prefix="$PREFIX" --default-library=static >/dev/null && \
        ninja -j"$JOBS" >/dev/null && ninja install >/dev/null)
    fi
}
if [[ ! -f "$PREFIX/lib/libz.a" ]]; then
    git_fetch_pin "$ZLIB_URL" "$ZLIB_SHA" "$TP/zlib" >/dev/null
    rec "zlib=$ZLIB_SHA"
    (cd "$TP/zlib" && CHOST=arm-vita-eabi CC="$VITABIN/arm-vita-eabi-gcc" \
        AR="$VITABIN/arm-vita-eabi-ar" ./configure --prefix="$PREFIX" --static >/dev/null \
        && make -j"$JOBS" >/dev/null && make install >/dev/null)
fi
if [[ ! -f "$PREFIX/lib/libbz2.a" ]]; then
    # Build bzip2 1.0.8 from its pinned source revision.
    git_fetch_pin "$BZIP2_URL" "$BZIP2_SHA" "$TP/bzip2" >/dev/null
    rec "bzip2=$BZIP2_SHA"
    log "bzip2 configure+build"
    # Library only: the bzip2/bunzip2 CLI binaries need fchown (absent on
    # Vita) and are not required. Install libbz2.a + headers by hand.
    (cd "$TP/bzip2" && make -j"$JOBS" libbz2.a CC="$VITABIN/arm-vita-eabi-gcc" \
        AR="$VITABIN/arm-vita-eabi-ar" RANLIB="$VITABIN/arm-vita-eabi-ranlib" \
        CFLAGS="$TP_CFLAGS" >/dev/null && \
        install -Dm644 libbz2.a "$PREFIX/lib/libbz2.a" && \
        install -Dm644 bzlib.h "$PREFIX/include/bzlib.h")
    rec "bzip2=1.0.8-source"
fi
# bzip2's Makefile installs no pkg-config metadata. FreeType may require it
# when it detects the workspace archive, so provide metadata for that archive.
printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\nName: bzip2\nDescription: bzip2 Vita static library\nVersion: 1.0.8\nLibs: -L${libdir} -lbz2\nCflags: -I${includedir}\n' "$PREFIX" > "$PKGDIR/bzip2.pc"
if [[ ! -f "$PREFIX/lib/libpng.a" ]]; then
    git_fetch_pin "$LIBPNG_URL" "$LIBPNG_SHA" "$TP/libpng" >/dev/null
    rec "libpng=$LIBPNG_SHA"
    build_autotools libpng "$TP/libpng"
fi
if [[ ! -f "$PREFIX/lib/libogg.a" ]]; then
    git_fetch_pin "$OGG_URL" "$OGG_SHA" "$TP/ogg" >/dev/null
    rec "ogg=$OGG_SHA"
    [[ -x "$TP/ogg/autogen.sh" ]] && (cd "$TP/ogg" && ./autogen.sh >/dev/null 2>&1 || true)
    build_autotools ogg "$TP/ogg"
fi
if [[ ! -f "$PREFIX/lib/libvorbis.a" ]]; then
    git_fetch_pin "$VORBIS_URL" "$VORBIS_SHA" "$TP/vorbis" >/dev/null
    rec "vorbis=$VORBIS_SHA"
    mkdir -p "$TP/vorbis/cmakebuild"
    # NOTE: the Vita toolchain file restricts find roots to $VITASDK, so pass
    # workspace ogg locations explicitly (same reason any workspace dep of a
    # cmake project needs explicit paths).
    (cd "$TP/vorbis/cmakebuild" && cmake .. -DCMAKE_TOOLCHAIN_FILE="$CAND_SRC/linux/toolchain-vita.cmake" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_C_FLAGS="$TP_CFLAGS" -DCMAKE_CXX_FLAGS="$TP_CFLAGS" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_SHARED_LIBS=no \
        -DOGG_INCLUDE_DIR="$PREFIX/include" -DOGG_LIBRARY="$PREFIX/lib/libogg.a" >/dev/null \
        && make -j"$JOBS" >/dev/null && make install >/dev/null)
fi
if [[ ! -f "$PREFIX/lib/libtheora.a" ]]; then
    git_fetch_pin "$THEORA_URL" "$THEORA_SHA" "$TP/theora" >/dev/null
    rec "theora=$THEORA_SHA"
    [[ -x "$TP/theora/autogen.sh" ]] && (cd "$TP/theora" && ./autogen.sh >/dev/null 2>&1 || true)
    build_autotools theora "$TP/theora" --with-ogg="$PREFIX" --disable-examples
fi
if [[ ! -f "$PREFIX/lib/libpixman-1.a" ]]; then
    git_fetch_pin "$PIXMAN_URL" "$PIXMAN_SHA" "$TP/pixman" >/dev/null
    rec "pixman=$PIXMAN_SHA"
    [[ -x "$TP/pixman/autogen.sh" ]] && (cd "$TP/pixman" && ./autogen.sh >/dev/null 2>&1 || true)
    build_autotools pixman "$TP/pixman" --disable-arm-simd  # Vita gcc rejects pixman ARM SIMD asm
fi
if [[ ! -f "$PREFIX/lib/libfreetype.a" ]]; then
    git_fetch_pin "$FREETYPE_URL" "$FREETYPE_SHA" "$TP/freetype" >/dev/null
    rec "freetype=$FREETYPE_SHA"
    [[ -x "$TP/freetype/autogen.sh" ]] && (cd "$TP/freetype" && ./autogen.sh >/dev/null 2>&1 || true)
    build_autotools freetype "$TP/freetype" --without-harfbuzz
fi
if [[ ! -f "$PREFIX/lib/libphysfs.a" ]]; then
    git_fetch_pin "$PHYSFS_URL" "$PHYSFS_SHA" "$TP/physfs" >/dev/null
    rec "physfs=$PHYSFS_SHA"
    mkdir -p "$TP/physfs/cmakebuild"
    # Vita has no CD-ROM and no mntent.h; -D__linux makes physfs assume Linux
    # mount-table enumeration, so declare NO_CDROM explicitly (same precedent
    # as physfs ANDROID/EMSCRIPTEN ports).
    cp "$DEPS_DIR/physfs_platform_vita.c" "$TP/physfs/src/physfs_platform_vita.c"
    if (cd "$TP/physfs" && patch -p1 -N --dry-run < "$DEPS_DIR/patches/physfs/3.2.0-vita-platform.patch" >/dev/null 2>&1); then
        (cd "$TP/physfs" && patch -p1 < "$DEPS_DIR/patches/physfs/3.2.0-vita-platform.patch" >/dev/null)
    else
        log "physfs vita patch already applied (or tree state); continuing"
    fi
    (cd "$TP/physfs/cmakebuild" && cmake .. -DCMAKE_TOOLCHAIN_FILE="$CAND_SRC/linux/toolchain-vita.cmake" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_C_FLAGS="$TP_CFLAGS -DPHYSFS_NO_CDROM_SUPPORT=1" -DCMAKE_CXX_FLAGS="$TP_CFLAGS" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DPHYSFS_BUILD_STATIC=true -DPHYSFS_BUILD_SHARED=false -DPHYSFS_BUILD_TEST=false -DPHYSFS_VITA_BACKEND=ON >/dev/null \
        && make -j"$JOBS" >/dev/null && make install >/dev/null)
fi
if [[ ! -f "$PREFIX/lib/libuchardet.a" ]]; then
    git_fetch_pin "$UCHARDET_URL" "$UCHARDET_SHA" "$TP/uchardet" >/dev/null
    rec "uchardet=$UCHARDET_SHA"
    mkdir -p "$TP/uchardet/cmakebuild"
    (cd "$TP/uchardet/cmakebuild" && cmake .. -DCMAKE_TOOLCHAIN_FILE="$CAND_SRC/linux/toolchain-vita.cmake" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_C_FLAGS="$TP_CFLAGS" -DCMAKE_CXX_FLAGS="$TP_CFLAGS" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_SHARED_LIBS=no >/dev/null \
        && make -j"$JOBS" >/dev/null && make install >/dev/null)
fi
if [[ ! -f "$PREFIX/lib/libiconv.a" ]]; then
    if [[ ! -d "$TP/libiconv-1.18" ]]; then
        (cd "$TP" && wget -q -O libiconv-1.18.tar.gz https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz && sha_check libiconv-1.18.tar.gz "$LIBICONV_SHA256" && tar -xzf libiconv-1.18.tar.gz)
    fi
    # libiconv: build ONLY lib/ (the conversion library). srclib/ (gnulib
    # helpers for the iconv CLI) does not compile against Vita newlib
    # (wint_t clash); the CLI is not needed. Install by hand.
    (cd "$TP/libiconv-1.18" && ./configure --host=arm-vita-eabi --prefix="$PREFIX" \
        --enable-static=yes --enable-shared=no CC="$VITABIN/arm-vita-eabi-gcc" \
        AR="$VITABIN/arm-vita-eabi-ar" CFLAGS="$TP_CFLAGS" >/dev/null && \
        : "libcharset first, fully installed: lib/ needs its installed headers" && \
        make -C libcharset -j"$JOBS" CC="$VITABIN/arm-vita-eabi-gcc" \
            AR="$VITABIN/arm-vita-eabi-ar" RANLIB="$VITABIN/arm-vita-eabi-ranlib" \
            CFLAGS="$TP_CFLAGS" >/dev/null && \
        make -C libcharset install CC="$VITABIN/arm-vita-eabi-gcc" \
            AR="$VITABIN/arm-vita-eabi-ar" RANLIB="$VITABIN/arm-vita-eabi-ranlib" \
            CFLAGS="$TP_CFLAGS" prefix="$PREFIX" exec_prefix="$PREFIX" \
            libdir="$PREFIX/lib" includedir="$PREFIX/include" >/dev/null && \
        make -C lib -j"$JOBS" CC="$VITABIN/arm-vita-eabi-gcc" AR="$VITABIN/arm-vita-eabi-ar" \
        RANLIB="$VITABIN/arm-vita-eabi-ranlib" CFLAGS="$TP_CFLAGS" >/dev/null && \
        install -Dm644 lib/.libs/libiconv.a "$PREFIX/lib/libiconv.a" && \
        install -Dm644 include/iconv.h "$PREFIX/include/iconv.h")
fi
rec "third-party-prefix=$PREFIX"

# ---------------------------------------------------------------- Ruby
log "ruby-vita"
RUBY_SHA="a2d396ea42e1ec778bc516489afe7fde6f0cef5d"
RUBY_PATCH="$DEPS_DIR/ruby-vita-a2d396e-clean.patch"
RUBY_COMPAT="$DEPS_DIR/ruby-vita-compat.h"
RUBY_SRC_DIR="$SRC/ruby-vita"
if [[ ! -f "$PREFIX/lib/libruby-3.1.a" ]]; then
    rm -rf "$RUBY_SRC_DIR"; mkdir -p "$RUBY_SRC_DIR"
    # Use the bundled source snapshot, verified by SHA-256.
    TARBALL="$DEPS_DIR/vendor/ruby-vita-a2d396e-tree.tar.zst"
    sha_check "$TARBALL" "$RUBY_SNAPSHOT_SHA256"
    zstd -dc "$TARBALL" | tar -xf - -C "$RUBY_SRC_DIR"
    git -C "$RUBY_SRC_DIR" init -q
    rec "ruby-base=tree-tar sha256=$RUBY_SNAPSHOT_SHA256"
    (cd "$RUBY_SRC_DIR" && git apply --check "$RUBY_PATCH" && git apply "$RUBY_PATCH")
    # configure + aux files are generated (not tracked): regenerate with
    # host autoconf; config.guess/sub come from system automake.
    cp /usr/share/automake-*/config.guess /usr/share/automake-*/config.sub "$RUBY_SRC_DIR/" 2>/dev/null || \
        { printf 'ERROR: need system automake config.guess/sub
' >&2; exit 1; }
    mkdir -p "$RUBY_SRC_DIR/tool"
    cp /usr/share/automake-*/config.guess /usr/share/automake-*/config.sub "$RUBY_SRC_DIR/tool/" 2>/dev/null || true
    # configure.ac carve-out (workspace copy only): Vita is static-only
    # bare-metal; suppress upstream's default -fPIC for non-listed targets.
    # (File-level sed does not survive make's enc.mk regeneration.)
    python3 - "$RUBY_SRC_DIR/configure.ac" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = """    [
      RUBY_APPEND_OPTION(CCDLFLAGS, -fPIC)])
  ], ["""
new = """    [
      AS_CASE(["$target_os"],
        [*vita*|*eabi], [],
        [RUBY_APPEND_OPTION(CCDLFLAGS, -fPIC)])])
  ], ["""
assert old in s, "configure.ac context changed; aborting"
open(p, "w").write(s.replace(old, new))
print("configure.ac Vita carve-out applied", file=sys.stderr)
PYEOF
    (cd "$RUBY_SRC_DIR" && autoconf >/dev/null 2>&1)
    rec "ruby-patch=$(sha256sum "$RUBY_PATCH" | cut -d' ' -f1)"
    cp "$RUBY_COMPAT" "$RUBY_SRC_DIR/vita-compat.h"
    rec "ruby-compat=$(sha256sum "$RUBY_COMPAT" | cut -d' ' -f1)"
    # Vita newlib lacks dup2/waitpid. The final engine link resolves these
    # references via vita_posix_compat.c. Preset only those configure tests.
    export ac_cv_func_dup2=yes ac_cv_func_waitpid=yes
    # Static-only bare-metal target: strip configure's default -fPIC.
    # PIC objects emit GOT relocs that vita-elf-create cannot process, and
    # nothing is shared. Only enc.mk injects it into object builds (ext
    # Makefiles do not); rbconfig.rb is fixed too so later ext builds agree.
    # ELF backtrace addr2line needs dladdr+dli_saddr, which are absent on
    # Vita. Exclude addr2line.o from the static Ruby build.
    export rb_cv_binary_elf=no
    # Extension set: cross have_func checks cannot link libruby yet, so exts
    # probing rb_* C funcs (bigdecimal et al.) misdetect and break. The
    # supported extensions are zlib, digest(main), coverage, cgi/escape,
    # continuation and date, plus enc/trans. The archive member manifest
    # in deps/ruby-vita-archive-members.txt defines the expected layout.
    # out-ext matching needs full subpaths for grouped exts (io/*, racc/*).
    # zlib is required (engine requires it); a game needing json/psych later
    # gets its own proof.
    (cd "$RUBY_SRC_DIR" && \
        ./configure --host=arm-vita-eabi --target=arm-vita-eabi \
            --with-baseruby=/usr/bin/ruby --prefix="$PREFIX" \
            --disable-shared --enable-static --enable-install-static-library \
            --disable-install-doc --disable-install-rdoc --disable-install-capi \
            --disable-rubygems --with-static-linked-ext --without-gmp \
            --with-out-ext=readline,dbm,gdbm,win32,win32ole,fiddle,bigdecimal,etc,fcntl,json,monitor,nkf,objspace,openssl,pathname,psych,pty,ripper,socket,stringio,strscan,syslog,sdbm,io/console,io/nonblock,io/wait,racc/cparse,rbconfig/sizeof,rubyvm,digest/bubblebabble,digest/md5,digest/rmd160,digest/sha1,digest/sha2 \
            --disable-jit-support --without-git \
            host_alias=arm-vita-eabi target_alias=arm-vita-eabi \
            AR="$VITABIN/arm-vita-eabi-ar" CC="$VITABIN/arm-vita-eabi-gcc" \
            CXX="$VITABIN/arm-vita-eabi-g++" RANLIB="$VITABIN/arm-vita-eabi-ranlib" \
            "CFLAGS=$ARCH_FLAGS -O2 -fno-strict-aliasing -Wno-error=incompatible-pointer-types -std=gnu99 -D__vita__ -I$PREFIX/include" \
            "LDFLAGS=-L$PREFIX/lib -Wl,-q" LIBS=-lm \
            "CPPFLAGS=-include $RUBY_SRC_DIR/vita-compat.h -I$PREFIX/include" \
            "CXXFLAGS=$ARCH_FLAGS -O2 -fno-strict-aliasing" >/dev/null && \
        : "BASERUBY keeps upstream --disable=gems (avoids tree rbconfig vs \
           host version clash). erb then hides when it lives in user gems, so \
           vendor pure-ruby erb into the workspace and expose via RUBYLIB." && \
        mkdir -p "$BUILD/ruby-gemlib" && \
        ERB_LIB="$(/usr/bin/ruby -e 'require "erb"; puts $LOADED_FEATURES.grep(%r{/erb\.rb$}).first' 2>/dev/null | xargs dirname 2>/dev/null)" && \
        { [[ -n "$ERB_LIB" && -d "$ERB_LIB" ]] || { printf 'ERROR: host erb not found for build tools\n' >&2; exit 1; }; } && \
        cp "$ERB_LIB/erb.rb" "$BUILD/ruby-gemlib/" && \
        { [[ ! -d "$ERB_LIB/erb" ]] || cp -r "$ERB_LIB/erb" "$BUILD/ruby-gemlib/"; } && \
        export RUBYLIB="$BUILD/ruby-gemlib" && \
        : "cross install is granular: the target miniruby/ruby PROGRAM links \
           cannot succeed on host (missing dup2/waitpid/getpagesize; resolved \
           later at engine link via vita_posix_compat.c) and are not needed. \
           Build objects+exts+encs, archive, pc-data, then install everything \
           except bin/: headers, archive and pkg-config metadata, no binaries." && \
        : "throwaway link stubs: the miniruby FILE is a make prerequisite for \
           codegen (never executed in cross mode; MINIRUBY var = baseruby). \
           newlib lacks dup2/waitpid/getpagesize, so link a stub object that \
           is used ONLY for the miniruby/PROGRAM program links. It never \
           enters libruby-3.1.a (pure ar) and is never installed; the real \
           U refs resolve at engine link via vita_posix_compat.c." && \
        cat > "$BUILD/ruby-miniruby-link-stubs.c" <<'STUBEOF'
/* Throwaway link-only stubs for the unrunnable cross miniruby/PROGRAM links.
 * NEVER installed, NEVER archived. Real implementations live in the engine
 * (vita_posix_compat.c) and resolve at final link. */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
int dup2(int o, int n) { (void)o; (void)n; errno = ENOSYS; return -1; }
pid_t waitpid(pid_t p, int *s, int o) { (void)p; (void)s; (void)o; errno = ENOSYS; return -1; }
int getpagesize(void) { return 4096; }
char *getlogin(void) { return (char *)"vita"; }
pid_t getpgrp(void) { errno = ENOSYS; return -1; }
unsigned int umask(unsigned int m) { (void)m; return 0; }
int execv(const char *p, char *const a[]) { (void)p; (void)a; errno = ENOSYS; return -1; }
struct passwd; struct passwd *getpwnam(const char *n) { (void)n; return 0; }
void endpwent(void) {}
int sigprocmask(int h, const void *s, void *o) { (void)h; (void)s; (void)o; return 0; }
pid_t getppid(void) { return 1; }
int execl(const char *p, const char *a, ...) { (void)p; (void)a; errno = ENOSYS; return -1; }
int execle(const char *p, const char *a, ...) { (void)p; (void)a; errno = ENOSYS; return -1; }
void *mmap(void *a, unsigned long n, int pr, int fl, int fd, long off) {
    (void)a; (void)n; (void)pr; (void)fl; (void)fd; (void)off;
    errno = ENOSYS; return (void *)-1;
}
int munmap(void *a, unsigned long n) { (void)a; (void)n; errno = ENOSYS; return -1; }
int mprotect(void *a, unsigned long n, int pr) { (void)a; (void)n; (void)pr; errno = ENOSYS; return -1; }
STUBEOF
        "$VITABIN/arm-vita-eabi-gcc" $ARCH_FLAGS -O2 -D__vita__ -c \
            "$BUILD/ruby-miniruby-link-stubs.c" -o "$BUILD/ruby-miniruby-link-stubs.o" && \
        : "make overrides RUBYLIB internally (common.mk), so pass it on the \
           make command line to survive into recipes." && \
        make -j"$JOBS" rbconfig.rb enc.mk \
            LIBS="-lm -lm $BUILD/ruby-miniruby-link-stubs.o" \
            RUBYLIB="$BUILD/ruby-gemlib" >/dev/null && \
        sed -i 's/ -fPIC//g; s/-fPIC //g' "$RUBY_SRC_DIR/enc.mk" "$RUBY_SRC_DIR/rbconfig.rb" && \
        make -j"$JOBS" main LIBS="-lm -lm $BUILD/ruby-miniruby-link-stubs.o" \
            RUBYLIB="$BUILD/ruby-gemlib" >/dev/null && \
        RUBY_STUB_O="$BUILD/ruby-miniruby-link-stubs.o" && \
        make -j"$JOBS" libruby-static.a RUBYLIB="$BUILD/ruby-gemlib" LIBS="-lm -lm $RUBY_STUB_O" >/dev/null && \
        : "PKG_CONFIG for target ruby is the SDK wrapper, which wipes \
           PKG_CONFIG_PATH and cannot see the just-written ruby.tmp.pc; use \
           host pkg-config for this validation/generation step only." && \
        make pkgconfig-data RUBYLIB="$BUILD/ruby-gemlib" LIBS="-lm -lm $RUBY_STUB_O" \
            PKG_CONFIG=/usr/bin/pkg-config >/dev/null && \
        : "Deterministic archive merge using the checked-in member manifest: \
           upstream only archives core OBJS (dmy placeholders); enc/trans/ext \
           objects are merged with collision-avoiding prefixes (enc_X, trans_X, \
           ext_D_X). thread_pthread.o has no symbols in this source snapshot \
           and is omitted." && \
        "$SCRIPT_DIR/ruby-merge-archive.sh" "$RUBY_SRC_DIR" "$PREFIX" "$VITABIN" && \
        : "Manual header install avoids cross-build make install-* failures \
           from circular do-install-arch dependencies and program links." && \
        "$SCRIPT_DIR/ruby-install-headers.sh" "$RUBY_SRC_DIR" "$PREFIX")
    # Upstream's generated .pc suits shared builds (empty Libs for static).
    # Write the pkg-config metadata for the static archive instead.
    printf 'prefix=%s\nexec_prefix=${prefix}\nlibdir=${prefix}/lib\nincludedir=${prefix}/include/ruby-3.1.0\n\nName: ruby-3.1\nDescription: Ruby (Vita static)\nVersion: 3.1.0\nRequires:\nLibs: -L${libdir} -lruby-3.1\nCflags: -I${includedir}\n' \
        "$PREFIX" > "$PKGDIR/ruby-3.1.pc"
    "$VITABIN/arm-vita-eabi-nm" -g --defined-only "$PREFIX/lib/libruby-3.1.a" \
        | grep -E 'ruby_(options|executable_node|exec_node)' >/dev/null \
        || { printf 'ERROR: libruby missing MRI entry points\n' >&2; exit 1; }
fi
rec "ruby-lib=$(sha256sum "$PREFIX/lib/libruby-3.1.a" | cut -d' ' -f1)"

# ---------------------------------------------------------------- vitaShaRK
log "vitaShaRK"
VITASHARK_SRC="$SRC/vitaShaRK"
if [[ ! -f "$PREFIX/lib/libvitashark.a" ]]; then
    rm -rf "$VITASHARK_SRC"; mkdir -p "$VITASHARK_SRC"
    git_fetch_pin "$VITASHARK_URL" "$VITASHARK_SHA" "$VITASHARK_SRC" >/dev/null
    git -C "$VITASHARK_SRC" archive "$VITASHARK_SHA" | tar -x -C "$VITASHARK_SRC"
    CPATH="$DEPS_DIR${CPATH:+:$CPATH}" make -C "$VITASHARK_SRC" -j"$JOBS" >/dev/null
    # VitaSDK has no SceShaccCgExt stubs. Add the local no-op compatibility
    # implementation, which stock VitaShaRK does not include.
    "$VITABIN/arm-vita-eabi-gcc" -c "$DEPS_DIR/shacccg_ext_stub.c" \
        -o "$BUILD/shacccg_ext_stub.o"
    "$VITABIN/arm-vita-eabi-ar" rcs "$VITASHARK_SRC/libvitashark.a" \
        "$BUILD/shacccg_ext_stub.o"
    install -Dm644 "$VITASHARK_SRC/libvitashark.a" "$PREFIX/lib/libvitashark.a"
    install -Dm644 "$VITASHARK_SRC/source/vitashark.h" "$PREFIX/include/vitashark.h"
fi
rec "vitashark=$VITASHARK_SHA lib=$(sha256sum "$PREFIX/lib/libvitashark.a" | cut -d' ' -f1)"

# ---------------------------------------------------------------- math-neon (vitaGL dependency)
log "math-neon"
MATH_NEON_SRC="$SRC/math-neon"
if [[ ! -f "$PREFIX/lib/libmathneon.a" ]]; then
    mkdir -p "$MATH_NEON_SRC"
    git_fetch_pin "$MATH_NEON_URL" "$MATH_NEON_SHA" "$MATH_NEON_SRC" >/dev/null
    make -C "$MATH_NEON_SRC" -j"$JOBS" >/dev/null
    install -Dm644 "$MATH_NEON_SRC/libmathneon.a" "$PREFIX/lib/libmathneon.a"
    install -Dm644 "$MATH_NEON_SRC/source/math_neon.h" "$PREFIX/include/math_neon.h"
fi
rec "math-neon=$MATH_NEON_SHA lib=$(sha256sum "$PREFIX/lib/libmathneon.a" | cut -d' ' -f1)"

# ---------------------------------------------------------------- vitaGL
log "vitaGL ($PROFILE)"
VITAGL_PATCH="$DEPS_DIR/patches/vitaGL/7f16d15c-vita-mkxp.patch"
VITAGL_SRC="$SRC/vitaGL"
VITAGL_RECIPE="source=$VITAGL_SHA patch=$(sha256sum "$VITAGL_PATCH" | cut -d' ' -f1) profile=$PROFILE NO_SPLASHSCREEN=1"
VITAGL_STAMP="$PREFIX/lib/libvitaGL.build-inputs"
if [[ ! -f "$PREFIX/lib/libvitaGL.a" || ! -f "$VITAGL_STAMP" || "$(cat "$VITAGL_STAMP")" != "$VITAGL_RECIPE" ]]; then
    rm -rf "$VITAGL_SRC"; mkdir -p "$VITAGL_SRC"
    git_fetch_pin "$VITAGL_URL" "$VITAGL_SHA" "$VITAGL_SRC" >/dev/null
    git -C "$VITAGL_SRC" archive "$VITAGL_SHA" | tar -x -C "$VITAGL_SRC"
    (cd "$VITAGL_SRC" && patch -p1 --dry-run < "$VITAGL_PATCH" >/dev/null && patch -p1 < "$VITAGL_PATCH" >/dev/null)
    rec "vitagl-patch=$(sha256sum "$VITAGL_PATCH" | cut -d' ' -f1)"
    if [[ "$PROFILE" == real-vita ]]; then VGL_FLAGS=(SHARED_RENDERTARGETS=2); else VGL_FLAGS=(HAVE_VITA3K_SUPPORT=1); fi
    VGL_FLAGS+=(NO_SPLASHSCREEN=1)
    CPATH="$PREFIX/include${CPATH:+:$CPATH}" make -C "$VITAGL_SRC" -B "${VGL_FLAGS[@]}" -j"$JOBS" >/dev/null
    install -Dm644 "$VITAGL_SRC/libvitaGL.a" "$PREFIX/lib/libvitaGL.a"
    install -Dm644 "$VITAGL_SRC/source/vitaGL.h" "$PREFIX/include/vitaGL.h"
    printf '%s\n' "$VITAGL_RECIPE" > "$VITAGL_STAMP"
fi
if "$VITABIN/arm-vita-eabi-nm" -g "$PREFIX/lib/libvitaGL.a" | grep -E '[[:space:]]invoke_splashscreen$' >/dev/null; then
    printf 'ERROR: vitaGL archive still contains the splashscreen entry point\n' >&2
    exit 1
fi
rec "vitagl=$VITAGL_SHA profile=$PROFILE lib=$(sha256sum "$PREFIX/lib/libvitaGL.a" | cut -d' ' -f1)"

# ---------------------------------------------------------------- shim
log "vita-sdl2-shim"
[[ -f "$SHIM_SRC/CMakeLists.txt" ]] || { printf 'ERROR: missing SDL shim source %s\n' "$SHIM_SRC" >&2; exit 1; }
rec "shim-src=$SHIM_SRC"
SHIM_BUILD="$BUILD/shim"
cmake -S "$SHIM_SRC" -B "$SHIM_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$CAND_SRC/linux/toolchain-vita.cmake" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_C_FLAGS="$TP_CFLAGS" -DCMAKE_CXX_FLAGS="$TP_CFLAGS" \
    -DVITA_DEP_PREFIX="$PREFIX" >/dev/null
cmake --build "$SHIM_BUILD" -j"$JOBS" >/dev/null
cmake --install "$SHIM_BUILD" >/dev/null
rec "shim-lib=$(sha256sum "$PREFIX/lib/libSDL2.a" | cut -d' ' -f1)"

# ---------------------------------------------------------------- openal
log "openal-vita"
OPENAL_SRC="$DEPS_DIR/openal-vita.c"
"$VITABIN/arm-vita-eabi-gcc" $ARCH_FLAGS -O2 -D__vita__ \
    -I"$PREFIX/include" -I"$PREFIX/include/SDL2" -I"$SHIM_SRC/include" -I"$SHIM_SRC/include/SDL2" \
    -I"$DEPS_DIR" \
    -c "$OPENAL_SRC" -o "$BUILD/openal-vita.o"
"$VITABIN/arm-vita-eabi-ar" rcs "$PREFIX/lib/libopenal.a" "$BUILD/openal-vita.o"
"$VITABIN/arm-vita-eabi-ranlib" "$PREFIX/lib/libopenal.a"
printf 'prefix=%s\nexec_prefix=${prefix}\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\n\nName: openal\nDescription: OpenAL (Vita minimal)\nVersion: 1.23.0\nRequires:\nLibs: -L${libdir} -lopenal\nCflags: -I${includedir}\n' \
    "$PREFIX" > "$PKGDIR/openal.pc"
rec "openal-src=$(sha256sum "$OPENAL_SRC" | cut -d' ' -f1)"

touch "$WS/bootstrap-complete"
log "bootstrap complete: $WS"
cat "$MANIFEST"
