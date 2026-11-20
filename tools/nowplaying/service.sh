#!/bin/sh
# Runs Now Playing in the background at login, so it never has to be started by hand. It connects over
# Wi-Fi when the board is listening for it, otherwise over the USB cable.
#   ./service.sh install    start now and at every login
#   ./service.sh stop       pause it (do this before flashing the board: it holds the USB port)
#   ./service.sh start      resume
#   ./service.sh uninstall  remove it
#   ./service.sh status | log
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
LABEL=com.crosspoint.nowplaying
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/nowplaying.log"
DOMAIN="gui/$(id -u)"

case "${1:-}" in
  install)
    [ -x "$HERE/.venv/bin/python" ] || { echo "Set up first: python3 -m venv .venv && .venv/bin/pip install -r requirements.txt"; exit 1; }
    mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
    cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array>
    <string>$HERE/.venv/bin/python</string><string>-u</string><string>$HERE/nowplaying.py</string><string>--wait</string><string>--wifi</string>
  </array>
  <key>WorkingDirectory</key><string>$HERE</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>10</integer>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict></plist>
EOF
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    launchctl bootstrap "$DOMAIN" "$PLIST"
    echo "Installed. It starts at every login. Log: $LOG"
    ;;
  start) launchctl bootstrap "$DOMAIN" "$PLIST" ;;
  stop) launchctl bootout "$DOMAIN/$LABEL" ;;
  uninstall)
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    rm -f "$PLIST"
    echo "Removed."
    ;;
  status) launchctl print "$DOMAIN/$LABEL" 2>/dev/null | grep -E "state|pid" || echo "not running" ;;
  log) tail -n 30 "$LOG" ;;
  *) echo "usage: $0 install|start|stop|uninstall|status|log"; exit 1 ;;
esac
