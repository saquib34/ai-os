#!/bin/bash
# Standalone Uninstaller for AI-OS
# Removes all AI-OS components, config, logs, and integration
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() {
  echo -e "${GREEN}[AI-OS Uninstall] $1${NC}"
}
warn() {
  echo -e "${YELLOW}[AI-OS Uninstall] $1${NC}"
}
error() {
  echo -e "${RED}[AI-OS Uninstall] $1${NC}"
}

log "Stopping and disabling ai-os service..."
sudo systemctl stop ai-os 2>/dev/null || true
sudo systemctl disable ai-os 2>/dev/null || true

log "Unloading kernel module (if loaded)..."
sudo rmmod ai_os 2>/dev/null || true

log "Removing installed binaries and files..."
sudo rm -f /usr/local/sbin/ai-os-daemon /usr/local/bin/ai-client
sudo rm -rf /usr/local/share/ai-os
sudo rm -f /etc/systemd/system/ai-os.service
sudo rm -rf /etc/ai-os /var/log/ai-os
sudo rm -f /lib/modules/$(uname -r)/extra/ai_os.ko
sudo rm -f /etc/logrotate.d/ai-os

log "Removing shell integration from ~/.bashrc..."
sed -i /ai-shell.sh/d ~/.bashrc || warn "Could not update ~/.bashrc"

log "Reloading systemd daemon..."
sudo systemctl daemon-reload

log "Clearing shell command cache..."
hash -r

log "AI-OS has been fully uninstalled." 