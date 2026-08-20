#!/usr/bin/env bash
# Build PlayerPhysics.dll (32-bit Windows) from macOS, Linux or Windows.
#
# Requirements: clang (Apple clang is fine) and lld-link from LLVM's lld.
#   macOS    brew install lld
#   Linux    the distro's clang and lld packages
#   Windows  the LLVM release, run from Git Bash -- see README
#
# The plugin links without a C runtime and without the Windows SDK, so no
# Visual Studio or xwin setup is needed -- the only import library is generated
# from scripts/kernel32.def on the fly. That is as true on Windows as it is
# anywhere else: this is not a cross-compile there, but nothing about the build
# changes.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/build"
target=i686-pc-windows-msvc

CLANG="${CLANG:-clang}"
LLD_LINK="${LLD_LINK:-lld-link}"

# On Homebrew, llvm-dlltool lives in the (keg-only) llvm prefix that lld depends
# on, so it is usually not on PATH. Everywhere else it sits beside clang, which
# is why clang's own directory is searched before the fixed candidates.
if [[ -z "${DLLTOOL:-}" ]]; then
    clang_bin="$(command -v "$CLANG" 2>/dev/null || true)"
    clang_dir="${clang_bin:+$(dirname "$clang_bin")}"

    for candidate in llvm-dlltool \
        ${clang_dir:+"$clang_dir/llvm-dlltool"} \
        /opt/homebrew/opt/llvm/bin/llvm-dlltool \
        /usr/local/opt/llvm/bin/llvm-dlltool \
        "/c/Program Files/LLVM/bin/llvm-dlltool"
    do
        if command -v "$candidate" >/dev/null 2>&1; then
            DLLTOOL="$candidate"
            break
        fi
    done
fi

for tool in "$CLANG" "$LLD_LINK" "${DLLTOOL:-llvm-dlltool}"; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: $tool not found. Install LLVM (macOS: brew install lld;" \
             "Windows: winget install LLVM.LLVM)." >&2
        exit 1
    }
done

# Git for Windows ships no zip, so fall back to 7-Zip, which is the one
# archiver that is reliably present there. Both produce a plain .zip; the mod
# managers that read it cannot tell the difference.
make_zip() {
    local out="$1"
    shift

    if command -v zip >/dev/null 2>&1; then
        zip -rq "$out" "$@" -x '*.DS_Store'
    elif command -v 7z >/dev/null 2>&1; then
        7z a -tzip -bso0 -bsp0 '-xr!.DS_Store' "$out" "$@"
    else
        echo "error: need zip or 7z to package $out" >&2
        exit 1
    fi
}

mkdir -p "$out"

cflags=(
    --target=$target
    -std=c++20
    -O2
    -flto=thin
    -fno-exceptions
    -fno-rtti
    -fno-stack-protector
    -mno-stack-arg-probe
    -ffreestanding
    -fms-extensions
    -fms-compatibility
    -Wall
    -Wno-unused-parameter
    -I"$root/src"
)

sources=(
    src/main.cpp
    src/config.cpp
    src/util/crt.cpp
    src/util/log.cpp
    src/util/paths.cpp
    src/util/patch.cpp
)

objects=()
for src in "${sources[@]}"; do
    obj="$out/$(basename "${src%.cpp}").obj"
    echo "  CXX $src"
    "$CLANG" "${cflags[@]}" -c "$root/$src" -o "$obj"
    objects+=("$obj")
done

# Import library for the kernel32 functions the plugin calls.
#
# --kill-at keeps the stdcall decoration on the object-side symbols (which is
# what clang emits) while exporting the plain names kernel32.dll actually has.
echo "  LIB kernel32"
"$DLLTOOL" -m i386 --kill-at \
    -d "$root/scripts/kernel32.def" -l "$out/kernel32.lib"

# lld-link's options are spelled with a leading dash rather than a slash: MSYS
# (Git Bash) rewrites any argument that looks like an absolute unix path into a
# Windows one, so "/dll" arrives as "C:/Program Files/Git/dll". lld accepts both
# spellings on every platform; the dash one is the only portable choice.
echo "  LNK PlayerPhysics.dll"
"$LLD_LINK" -machine:x86 -dll -nodefaultlib -entry:DllMain \
    -out:"$out/PlayerPhysics.dll" \
    "${objects[@]}" "$out/kernel32.lib"

# Stage the mod exactly as it sits under Data/, then zip it.
#
# The staging directories are wiped first: they are build output, and leftovers
# from an earlier layout would otherwise be picked up and shipped.
stage_dll="$root/nvse/Plugins"
stage_ini="$root/config/PlayerMovement"
main_archive="$root/PlayerPhysics.zip"
ini_archive="$root/PlayerPhysics - Config.zip"
ru_archive="$root/PlayerPhysics - Russian MCM.zip"
stage_ru="$root/.stage-ru"
stage_inputs=("$root/scripts-game" "$root/PlayerPhysics.ini" "$root/presets" "$root/MCM" "$root/MCM-RU" "$root/LICENSE")

# Refuse to touch the staging tree unless every input is present: the wipe
# below is destructive, and a missing source would otherwise leave nothing.
for required in "${stage_inputs[@]}"; do
    [[ -e "$required" ]] || { echo "error: missing $required" >&2; exit 1; }
done

rm -rf "$root/nvse" "$root/config"
mkdir -p "$stage_dll" "$stage_ini/presets"

cp "$out/PlayerPhysics.dll" "$stage_dll/"
mkdir -p "$root/nvse/Plugins/Scripts"
cp "$root"/scripts-game/*.txt "$root/nvse/Plugins/Scripts/"
cp "$root"/presets/*.ini "$stage_ini/presets/"
cp "$root/PlayerPhysics.ini" "$stage_ini/"


# JIP will not precompile a loose script with unix line endings, and it fails
# silently -- the script layer simply never announces itself and the plugin
# falls back to standing down for any special idle. The sources are kept as
# they are; only the staged copies are converted.
for f in "$root"/nvse/Plugins/Scripts/*.txt; do
    perl -pi -e 's/\r?\n/\r\n/' "$f"
done

# LICENSE travels with the binary: this is a GPL-3.0 fork and the licence text
# has to reach whoever gets it. The notice of what was changed and whose work it
# is a fork of lives on the mod page instead of in the archive.
# Three archives, each installable on its own and each ready to upload.
#
#   PlayerPhysics.zip
#       what the game needs, and nothing else.
#   PlayerPhysics - Config.zip
#       the ini, at the path it belongs at. Separate so that updating the mod
#       cannot overwrite settings the player has spent time on -- a mod manager
#       installs it once and leaves it alone afterwards.
#   PlayerPhysics - Russian MCM.zip
#       the Russian menu text, staged as MCM\Translations so it drops straight
#       in. Separate because it replaces a file the main archive installs, and
#       that is a thing a mod manager should be told about rather than hide.
#
# Documentation is not in any of them. It lives in the repository.
rm -rf "$stage_ru" "$main_archive" "$ini_archive" "$ru_archive"
mkdir -p "$stage_ru/MCM/Translations"
cp "$root/MCM-RU/Translations/PlayerPhysics.ini" "$stage_ru/MCM/Translations/"

(cd "$root"     && make_zip "$main_archive" nvse MCM LICENSE)
(cd "$root"     && make_zip "$ini_archive"  config)
(cd "$stage_ru" && make_zip "$ru_archive"   MCM)

rm -rf "$stage_ru"



echo
echo "built   $out/PlayerPhysics.dll"
echo "staged  $stage_dll/PlayerPhysics.dll"
echo "        $stage_ini/PlayerPhysics.ini"
echo "        $stage_ini/presets/"
echo "        $root/nvse/Plugins/Scripts/ (script layer)"
echo "        $root/MCM/ (menu)"
echo "        $root/MCM-RU/ (optional Russian menu text)"
echo "package $main_archive"
echo "        $ini_archive"
echo "        $ru_archive"
