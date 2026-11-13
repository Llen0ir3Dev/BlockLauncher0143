#!/usr/bin/env bash
# NEXUS-MCPE integration test.
#
# Exercises the real built executables end to end: CLI commands, the shader
# pipeline, MCPE asset parsing, worldgen determinism, the bridge socket, the
# launcher headless modes and (when the user supplied it) the real APK.
#
# No proprietary file is required.  If minecraft-0.14.3.apk is present it is
# inspected and imported; the original file is hashed before and after to
# prove that NEXUS never modifies it.
set -euo pipefail

NEXUS="${1:?nexus binary}"
RUNTIME="${2:?nexus-runtime binary}"
LAUNCHER="${3:?nexus-launcher binary}"
SOURCE_DIR="${4:?source directory}"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/nexus-integration.XXXXXX")"
export NEXUS_HOME="$TMP/home"
mkdir -p "$NEXUS_HOME"
cleanup() {
    if [[ -n "${RUNTIME_PID:-}" ]] && kill -0 "$RUNTIME_PID" 2>/dev/null; then
        kill "$RUNTIME_PID" 2>/dev/null || true
        wait "$RUNTIME_PID" 2>/dev/null || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

expect_grep() {
    local pattern="$1"
    local file="$2"
    grep -q -- "$pattern" "$file" || fail "expected '$pattern' in $file"
}

echo "== version and doctor"
"$NEXUS" version >"$TMP/version.txt"
expect_grep "Minecraft PE 0.14.3" "$TMP/version.txt"
"$NEXUS" doctor --json >"$TMP/doctor.json"
expect_grep '"architecture"' "$TMP/doctor.json"

echo "== worldgen determinism"
"$NEXUS" worldgen 12345 --chunk 0,0 >"$TMP/world-a.txt"
"$NEXUS" worldgen 12345 --chunk 0,0 >"$TMP/world-b.txt"
cmp -s "$TMP/world-a.txt" "$TMP/world-b.txt" || fail "worldgen is not deterministic"
expect_grep "dominant biome" "$TMP/world-a.txt"
"$NEXUS" worldgen 12345 --height 10,-4 >"$TMP/height.txt"
"$NEXUS" worldgen 12345 --biome 100,100 >"$TMP/biome.txt"
"$NEXUS" worldgen 12345 --json --chunk 0,0 >"$TMP/world.json"
expect_grep '"summary"' "$TMP/world.json"

echo "== shader translate / analyze / compile"
cat >"$TMP/test.fragment" <<'EOF'
precision mediump float;
varying vec2 v_texcoord;
uniform sampler2D TEXTURE_0;
uniform vec4 FOG_COLOR;
void main() {
    vec4 tex = texture2D(TEXTURE_0, v_texcoord);
    gl_FragColor = mix(tex, FOG_COLOR, 0.25);
}
EOF
"$NEXUS" shader translate "$TMP/test.fragment" --target gles100 --out "$TMP/out.fragment"
expect_grep "#version 100" "$TMP/out.fragment"
expect_grep "gl_FragColor" "$TMP/out.fragment"
"$NEXUS" shader analyze "$TMP/test.fragment" >"$TMP/analyze.txt"
expect_grep "Compatibility" "$TMP/analyze.txt"
"$NEXUS" shader compile "$TMP/test.fragment" >"$TMP/compile.txt"
expect_grep "compile OK" "$TMP/compile.txt"

echo "== shader multiversion compile (real MCPE layout)"
cat >"$TMP/multiversion.vertex" <<'EOF'
// __multiversion__
#if __VERSION__ >= 300
#define attribute in
#define varying out
#else
#endif
uniform MAT4 WORLDVIEWPROJ;
attribute POS4 POSITION;
varying vec4 color;
void main() {
    gl_Position = WORLDVIEWPROJ * POSITION;
    color = vec4(1.0);
}
EOF
"$NEXUS" shader compile "$TMP/multiversion.vertex" >"$TMP/mv-compile.txt"
expect_grep "compile OK" "$TMP/mv-compile.txt"
"$NEXUS" shader analyze "$TMP/multiversion.vertex" --multiversion 100 >"$TMP/mv-analyze.txt"
expect_grep "Compatibility" "$TMP/mv-analyze.txt"

echo "== MCPE asset inspection (material comments, inheritance, multiversion)"
mkdir -p "$TMP/assets/shaders" "$TMP/assets/materials"
cat >"$TMP/assets/shaders/uniforms.json" <<'EOF'
{
  "PerFrameConstants": [{"Name": "TIME", "Type": "Float1"}],
  "ShaderConstants": [{"Name": "WORLDVIEWPROJ", "Type": "Matrix4x4"}]
}
EOF
cat >"$TMP/assets/shaders/color.vertex" <<'EOF'
// __multiversion__
#if __VERSION__ >= 300
#define attribute in
#define varying out
#else
#endif
uniform MAT4 WORLDVIEWPROJ;
attribute POS4 POSITION;
varying vec4 color;
void main() { gl_Position = WORLDVIEWPROJ * POSITION; color = vec4(1.0); }
EOF
cat >"$TMP/assets/shaders/color.fragment" <<'EOF'
// __multiversion__
precision mediump float;
varying vec4 color;
void main() { gl_FragColor = color; }
EOF
cat >"$TMP/assets/materials/test.material" <<'EOF'
{
  // shipped MCPE material files contain comments and inheritance
  "test_base": {
    "vertexShader": "shaders/color.vertex",
    "fragmentShader": "shaders/color.fragment"
  },
  "test_blend:test_base": {
    "states": ["Blending"]
  }
}
EOF
"$NEXUS" mcpe info --assets "$TMP/assets" >"$TMP/mcpe.txt"
expect_grep "Material passes: 2" "$TMP/mcpe.txt"
expect_grep "Missing referenced shaders: 0" "$TMP/mcpe.txt"
"$NEXUS" mcpe info --assets "$TMP/assets" --json >"$TMP/materials.json"
expect_grep '"parent"' "$TMP/materials.json"

echo "== bridge agent roundtrip"
SOCKET="$TMP/bridge.sock"
"$RUNTIME" --headless --socket "$SOCKET" --assets "$TMP/assets" --seed 7 --seconds 30 \
    >"$TMP/runtime.log" 2>&1 &
RUNTIME_PID=$!
for _ in $(seq 1 100); do
    [[ -S "$SOCKET" ]] && break
    sleep 0.05
done
[[ -S "$SOCKET" ]] || {
    cat "$TMP/runtime.log" >&2 || true
    fail "runtime did not create $SOCKET"
}
"$NEXUS" bridge ping --socket "$SOCKET" --json >"$TMP/ping.json"
expect_grep '"ok": true' "$TMP/ping.json"
"$NEXUS" bridge status --socket "$SOCKET" --json >"$TMP/status.json"
expect_grep '"clients"' "$TMP/status.json"
"$NEXUS" bridge call runtime.info --socket "$SOCKET" >"$TMP/runtime-info.json"
expect_grep '"nexus-runtime"' "$TMP/runtime-info.json"
"$NEXUS" bridge call worldgen.sample --socket "$SOCKET" \
    --params '{"x":3,"z":4}' >"$TMP/worldgen-rpc.json"
expect_grep '"biome"' "$TMP/worldgen-rpc.json"
"$NEXUS" bridge call mcpe.assets --socket "$SOCKET" >"$TMP/mcpe-rpc.json"
expect_grep '"uniforms"' "$TMP/mcpe-rpc.json"
"$NEXUS" bridge call shutdown --socket "$SOCKET" >"$TMP/shutdown.json"
expect_grep '"stopping": true' "$TMP/shutdown.json"
for _ in $(seq 1 100); do
    kill -0 "$RUNTIME_PID" 2>/dev/null || break
    sleep 0.05
done
kill -0 "$RUNTIME_PID" 2>/dev/null && fail "runtime ignored shutdown"
wait "$RUNTIME_PID" 2>/dev/null || true
RUNTIME_PID=""

echo "== launcher headless modes"
"$LAUNCHER" --list --json --root "$TMP/installs" >"$TMP/launcher-list.json"
expect_grep '"installs"' "$TMP/launcher-list.json"
"$LAUNCHER" --report --json --root "$TMP/installs" --socket "$SOCKET" \
    >"$TMP/launcher-report.json"
expect_grep '"capabilities"' "$TMP/launcher-report.json"
"$LAUNCHER" --profiles --json >"$TMP/profiles.json"
expect_grep '"profiles"' "$TMP/profiles.json"

APK="$SOURCE_DIR/minecraft-0.14.3.apk"
if [[ -f "$APK" ]]; then
    echo "== real APK verification and installation"
    BEFORE="$(sha256sum "$APK" | awk '{print $1}')"
    [[ "$BEFORE" == "c4365666ac5c5a65af3c8a94d765e34dc2f525bbca787d9431e0aa091f70f0f6" ]] ||
        echo "note: APK hash differs from the reference hash known to this project" >&2
    "$NEXUS" apk info "$APK" --json >"$TMP/apk-info.json"
    expect_grep '"com.mojang.minecraftpe"' "$TMP/apk-info.json"
    if "$NEXUS" apk verify "$APK" >"$TMP/apk-verify.txt" 2>&1; then
        fail "ARM APK unexpectedly verified as runnable on x86"
    else
        CODE=$?
        [[ "$CODE" == "3" ]] || fail "apk verify exit code $CODE (expected 3)"
    fi
    expect_grep "valid-but-not-runnable-x86" "$TMP/apk-verify.txt"

    ROOT="$TMP/installs"
    "$NEXUS" install import "$APK" --root "$ROOT" --json >"$TMP/install.json"
    expect_grep '"id"' "$TMP/install.json"
    ID="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["id"])' "$TMP/install.json")"
    "$NEXUS" install list --root "$ROOT" --json >"$TMP/install-list.json"
    expect_grep "$ID" "$TMP/install-list.json"
    "$NEXUS" install verify "$ID" --root "$ROOT" >"$TMP/install-verify.txt"
    expect_grep "OK:" "$TMP/install-verify.txt"
    "$LAUNCHER" --list --json --root "$ROOT" >"$TMP/launcher-installs.json"
    expect_grep "$ID" "$TMP/launcher-installs.json"
    "$NEXUS" install remove "$ID" --root "$ROOT" >"$TMP/install-remove.txt"
    [[ ! -d "$ROOT/$ID" ]] || fail "installation directory was not removed"
    AFTER="$(sha256sum "$APK" | awk '{print $1}')"
    [[ "$BEFORE" == "$AFTER" ]] || fail "the original APK was modified"
else
    echo "== real APK skipped (not found at $APK)"
fi

echo "PASS integration"
