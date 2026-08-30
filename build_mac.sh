#!/bin/bash

set -e
cd "$(dirname "$0")"

# --- Unpack Arguments --------------------------------------------------------
for arg in "$@"; do declare $arg='1'; done
if [[ ! "$gcc" ]];     then clang=1; fi
if [[ ! "$release" ]]; then debug=1; fi
if [[ $debug ]];     then echo "[debug mode]"; fi
if [[ $release ]];   then echo "[release mode]"; fi

if [[ $bundle ]];    then raddbg=1; fi

# NOTE(yuraiz): On macOS /usr/bin/gcc is just an alias to clang anyway
compiler="${CC:-clang}"
echo "[clang compile]"

# --- Unpack Command Line Build Arguments -------------------------------------
auto_compile_flags=''
if [ -n "${instruments+x}" ]; then auto_compile_flags="$auto_compile_flags -DPROFILE_INSTRUMENTS=1" && echo "[instruments profiling enabled]"; fi

# --- Get Current Git Commit Id -----------------------------------------------
git_hash=$(git describe --always --dirty)
git_hash_full=$(git rev-parse HEAD)

# --- Compile/Link Line Definitions -------------------------------------------

# --- Per-Build Settings ------------------------------------------------------
link_render="-F/System/Library/PrivateFrameworks -framework QuartzCore -framework MetalPerformanceShaders -framework Metal -framework CoreFoundation -framework Cocoa -framework DebugSymbols"

# --- Choose Compile/Link Lines -----------------------------------------------
# NOTE(yuraiz): All warnings are disabled
clang_common="-ObjC -I../src/ -I../local/ -DBUILD_GIT_HASH=\"$git_hash\" -DBUILD_GIT_HASH_FULL=\"$git_hash_full\" -fdiagnostics-absolute-paths -Wno-pointer-sign -Wno-macro-redefined -Wno-unknown-warning-option -Wall -Wno-missing-braces -Wno-unused-function -Wno-writable-strings -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-register -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-single-bit-bitfield-constant-conversion -Wno-compare-distinct-pointer-types -Wno-initializer-overrides -Wno-incompatible-function-pointer-types -Wno-incompatible-pointer-types-discards-qualifiers -Wno-for-loop-analysis -Wno-switch -Wno-format -Xclang -flto-visibility-public-std -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf"
compile_debug="$compiler -g -O0 -DBUILD_DEBUG=1 ${clang_common} ${auto_compile_flags}"
compile_release="$compiler -g -O2 -DBUILD_DEBUG=0 ${clang_common} ${auto_compile_flags}"
compile_link="-lpthread -lm -ldl -lc++abi"
out="-o"
if [[ $debug ]];   then compile="$compile_debug"; fi
if [[ $release ]]; then compile="$compile_release"; fi

# --- Prep Directories --------------------------------------------------------
mkdir -p build
mkdir -p local

# --- Build & Run Mig files ---------------------------------------------------
if [[ $mig ]]
then
  echo "[generating mig files]"
  gen_dir="src/demon/mac/generated"
  mig -user "$gen_dir/mig_client.c" -server "$gen_dir/mig_server.c" -header "$gen_dir/mig_client.h" -sheader "$gen_dir/mig_server.h" src/demon/mac/mig.defs
fi

# --- Build & Run Metaprogram -------------------------------------------------
if [[ $meta ]]
then
  echo "[doing metagen]"
  cd build
  $compile_debug ../src/metagen/metagen_main.c $compile_link $out metagen
  ./metagen
  cd ..
fi

# --- Build Everything (@build_targets) ---------------------------------------
cd build
if [[ $raddbg ]];  then didbuild=1 && $compile ../src/raddbg/raddbg_main.c $compile_link $link_render $out raddbg; fi
if [[ $raddbg ]];  then codesign --entitlements ../data/macos.entitlements.plist -s - raddbg; fi
if [[ $radbin ]];  then didbuild=1 && $compile ../src/radbin/radbin_main.c $compile_link $out radbin; fi
if [[ $radlink ]]; then didbuild=1 && $compile ../src/linker/lnk.c         $compile_link $out radlink; fi
cd ..

# --- Warn On No Builds -------------------------------------------------------
if [[ ! $didbuild ]]
then
  echo "[WARNING] no valid build target specified; must use build target names as arguments to this script, like \`./build.sh raddbg\` or \`./build.sh rdi_from_pdb\`."
  exit 1
fi

# --- Create App Bundle -------------------------------------------------------
if [[ $bundle ]];
then
  echo "[bundling app]"
  #!/usr/bin/env bash

  # Configuration
  APP_NAME="The RAD Debugger"
  BIN_NAME="./build/raddbg"
  APP_BUNDLE="./build/${APP_NAME}.app"

  # 1. Create the directory structure
  rm -rf "$APP_BUNDLE"
  mkdir -p "$APP_BUNDLE/Contents/MacOS"
  mkdir -p "$APP_BUNDLE/Contents/Resources"

  # 2. Copy binary and assets
  cp "$BIN_NAME" "$APP_BUNDLE/Contents/MacOS/"
  cp -r "./data/mac_resources/" "$APP_BUNDLE/Contents/Resources"

  # 3. Create a minimal Info.plist
  cat <<'EOF' > "$APP_BUNDLE/Contents/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>raddbg</string>
    <key>CFBundleIconFile</key>
  	<string>raddbg</string>
  	<key>CFBundleIconName</key>
  	<string>raddbg</string>
    <key>CFBundleIdentifier</key>
    <string>com.epicgames.raddbg</string>
    <key>CFBundleName</key>
    <string>The RAD Debugger</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSApplicationCategoryType</key>
  	<string>public.app-category.developer-tools</string>
</dict>
</plist>
EOF

  plutil -convert xml1 "$APP_BUNDLE/Contents/Info.plist"

  # Make the Bundle executable
  chmod +x "$APP_BUNDLE"

  # Adhoc sign it for dev build
  codesign --force --entitlements "./data/macos.entitlements.plist" -s - "$APP_BUNDLE"

  # Verify if the Bundle is valid
  codesign -v --strict --verbose=2 "$APP_BUNDLE"

  echo "[app bundle created at $APP_BUNDLE]"
fi
