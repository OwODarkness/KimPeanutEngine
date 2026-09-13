#!/usr/bin/env bash
# One-off ED3 evidence helper: launch, capture, shut down. Not part of the build.
set -u
cd /d/C++Project/KimPeanutEngine

OUT="$1"
LAYOUT="$2"   # "none" or the JSON to write

taskkill //F //IM KimPeanutEngine.exe >/dev/null 2>&1
sleep 2
rm -f imgui.ini "save/screenshots/validation/${OUT}" \
      "save/screenshots/validation/${OUT%.png}-1.png"
if [ "$LAYOUT" = "none" ]; then
  rm -f save/editor_layout.json
else
  printf '%s\n' "$LAYOUT" > save/editor_layout.json
fi

./build/Debug/KimPeanutEngine.exe --graphics-api vulkan \
  --startup-level level/sponza.level --agent-port 37373 >/tmp/ed3-cap.log 2>&1 &
sleep 80

python - "$OUT" <<'PY'
import socket, sys, time
out = sys.argv[1]
def send(msg):
    s = socket.create_connection(('127.0.0.1', 37373), 5)
    s.sendall(msg.encode()); s.settimeout(10)
    return s.recv(65536).decode()

body = '{"op":"execute","command":"capture.screenshot","arguments":{"path":"save/screenshots/validation/%s","view":"engine_window"}}' % out
print(send(body))
time.sleep(6)
print(send('{"op":"poll","request_id":1}'))
PY

taskkill //F //IM KimPeanutEngine.exe >/dev/null 2>&1
echo "--- layout file after run ---"
cat save/editor_layout.json 2>/dev/null || echo "(none)"
