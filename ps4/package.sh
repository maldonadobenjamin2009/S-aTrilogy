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
# Pack all application data using the exact paths expected by app_create().
cp -a dist/main/. "$STAGE/main/"
cp -a dist/assets/. "$STAGE/assets/"
cp ps4/sce_sys/icon0.png "$STAGE/sce_sys/icon0.png"

SAMPLE="$SDK/samples/SDL2"
if [ ! -d "$SAMPLE/sce_module" ] || [ ! -f "$SAMPLE/sce_sys/about/right.prx" ]; then
  echo "ERROR: el toolchain no contiene samples/SDL2 con módulos de soporte" >&2
  exit 1
fi
cp -a "$SAMPLE/sce_module/." "$STAGE/sce_module/"
cp "$SAMPLE/sce_sys/about/right.prx" "$STAGE/sce_sys/about/right.prx"

# Convert ELF to the executable format used by PS4 homebrew packages.
(
  cd "$STAGE"
  "$CREATE_FSELF" -in="$ELF" -out="$STAGE/SonicAdvanceTrilogy.oelf" --eboot "eboot.bin" --paid 0x3800000000000011
)

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
FILES=$(find . -type f ! -name '*.pkg' ! -name 'pkg.gp4' ! -name '*.oelf' -printf '%P\n' | sort | tr '\n' ' ')
"$CREATE_GP4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "$FILES"
"$PKGTOOL" pkg_build pkg.gp4 .
PKG_PATH="$STAGE/$CONTENT_ID.pkg"
if [ ! -f "$PKG_PATH" ]; then
  PKG_PATH=$(find "$STAGE" -maxdepth 1 -type f -name '*.pkg' -print -quit)
fi
[ -n "${PKG_PATH:-}" ] && [ -f "$PKG_PATH" ] || { echo "ERROR: PkgTool.Core no generó un PKG" >&2; exit 1; }
mkdir -p "$(dirname "$STAGE")"
cp "$PKG_PATH" "$(dirname "$STAGE")/SonicAdvanceTrilogy-PS4.pkg"
echo "PKG creado: $(dirname "$STAGE")/SonicAdvanceTrilogy-PS4.pkg"
