#!/usr/bin/env bash
set -euo pipefail
ELF="${1:?ELF path required}"
STAGE="${2:?staging directory required}"
SDK="${3:?OpenOrbis SDK path required}"
CONTENT_ID="${4:?Content ID required}"
TITLE_ID="${5:?Title ID required}"
VERSION="${6:?Version required}"
TITLE="${7:?Title required}"
TOOLS="$SDK/bin/linux"
PKGTOOL="$TOOLS/PkgTool.Core"
CREATE_FSELF="$TOOLS/create-fself"
CREATE_GP4="$TOOLS/create-gp4"

rm -rf "$STAGE"
mkdir -p "$STAGE/sce_sys/about" "$STAGE/sce_module" "$STAGE/main" "$STAGE/assets"
# Pack application data while keeping game ROMs optional. Repository/CI builds
# stay ROM-free; a local build may include the user's own copies if present.
if [ -d dist/main ]; then
  cp -a dist/main/. "$STAGE/main/"
fi
mkdir -p "$STAGE/main/roms"
cp -a dist/assets/. "$STAGE/assets/"
cp ps4/sce_sys/icon0.png "$STAGE/sce_sys/icon0.png"

# Use the exact OpenOrbis Piglet sample runtime layout known to start correctly
# on GoldHEN. The official CI container omits its samples tree, so the workflow
# fetches these three small, version-pinned binary files into OO_PS4_SAMPLE_DIR.
SAMPLE_DIR="${OO_PS4_SAMPLE_DIR:-$SDK/samples/piglet}"
RIGHT_MODULE="${OO_PS4_RIGHT_MODULE:-$SAMPLE_DIR/sce_sys/about/right.sprx}"
if [ ! -s "$RIGHT_MODULE" ]; then
  for candidate in \
    "$SDK/samples/piglet/sce_sys/about/right.sprx" \
    "$SDK/samples/SDL2/sce_sys/about/right.sprx" \
    "ps4/sce_sys/about/right.sprx"; do
    if [ -s "$candidate" ]; then RIGHT_MODULE="$candidate"; break; fi
  done
fi
if [ ! -s "$RIGHT_MODULE" ]; then
  echo "ERROR: falta sce_sys/about/right.sprx. Define OO_PS4_SAMPLE_DIR con los archivos del sample Piglet de OpenOrbis." >&2
  exit 1
fi
cp "$RIGHT_MODULE" "$STAGE/sce_sys/about/right.sprx"

MODULE_DIR="$SDK/bin/data/modules"
for module in libc.prx libSceFios2.prx; do
  sample_module="$SAMPLE_DIR/sce_module/$module"
  if [ -s "$sample_module" ]; then
    cp "$sample_module" "$STAGE/sce_module/$module"
  elif [ -s "$MODULE_DIR/$module" ]; then
    cp "$MODULE_DIR/$module" "$STAGE/sce_module/$module"
  else
    echo "ERROR: falta el módulo requerido de OpenOrbis: $module (sample dir: $SAMPLE_DIR; SDK: $MODULE_DIR)" >&2
    exit 1
  fi
done
echo "OpenOrbis rights/runtime files included:"
ls -l "$STAGE/sce_sys/about/right.sprx" "$STAGE/sce_module/libc.prx" "$STAGE/sce_module/libSceFios2.prx"

# Convert ELF to the executable format used by PS4 homebrew packages.
(
  cd "$STAGE"
  "$CREATE_FSELF" -in="$ELF" -out="$STAGE/SonicAdvanceTrilogy.oelf" --eboot "eboot.bin" --paid 0x3800000000000011
)

# No basta con que create-fself termine con código 0: confirma que produjo el
# ejecutable real que se instalará en la consola, no solamente el ELF intermedio.
if [ ! -s "$STAGE/eboot.bin" ]; then
  echo "ERROR: create-fself terminó pero no generó un eboot.bin no vacío en $STAGE" >&2
  exit 1
fi
echo "eBoot PS4 generado: $(stat -c '%s' "$STAGE/eboot.bin") bytes"
file "$STAGE/eboot.bin" || true

# Create param.sfo via the supported OpenOrbis packaging utility.
"$PKGTOOL" sfo_new "$STAGE/sce_sys/param.sfo"
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" APP_TYPE --type Integer --maxsize 4 --value 1
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" APP_VER --type Utf8 --maxsize 8 --value "$VERSION"
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" ATTRIBUTE --type Integer --maxsize 4 --value 0
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" CATEGORY --type Utf8 --maxsize 4 --value gd
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" CONTENT_ID --type Utf8 --maxsize 48 --value "$CONTENT_ID"
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" SYSTEM_VER --type Integer --maxsize 4 --value 0
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" TITLE --type Utf8 --maxsize 128 --value "$TITLE"
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" TITLE_ID --type Utf8 --maxsize 12 --value "$TITLE_ID"
"$PKGTOOL" sfo_setentry "$STAGE/sce_sys/param.sfo" VERSION --type Utf8 --maxsize 8 --value "$VERSION"

# GP4 must list every file placed in the application package.
cd "$STAGE"
for required in eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/about/right.sprx sce_module/libc.prx sce_module/libSceFios2.prx; do
  if [ ! -s "$required" ]; then
    echo "ERROR: falta un archivo obligatorio del paquete PS4: $required" >&2
    exit 1
  fi
done
FILES=$(find . -type f ! -name '*.pkg' ! -name 'pkg.gp4' ! -name '*.oelf' -printf '%P\n' | sort | tr '\n' ' ')
"$CREATE_GP4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "$FILES"
"$PKGTOOL" pkg_build pkg.gp4 .
PKG_PATH="$STAGE/$CONTENT_ID.pkg"
if [ ! -f "$PKG_PATH" ]; then
  PKG_PATH=$(find "$STAGE" -maxdepth 1 -type f -name '*.pkg' -print -quit)
fi
[ -n "${PKG_PATH:-}" ] && [ -s "$PKG_PATH" ] || { echo "ERROR: PkgTool.Core no generó un PKG no vacío" >&2; exit 1; }
echo "PKG validado: $(stat -c '%s' "$PKG_PATH") bytes"
mkdir -p "$(dirname "$STAGE")"
cp "$PKG_PATH" "$(dirname "$STAGE")/SonicAdvanceTrilogy-PS4.pkg"
echo "PKG creado: $(dirname "$STAGE")/SonicAdvanceTrilogy-PS4.pkg"
