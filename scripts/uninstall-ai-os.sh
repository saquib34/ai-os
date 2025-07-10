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

log "Removing AI-OS source directory..."
# Remove the AI-OS project directory if it exists in common locations
for dir in ~/Desktop/ai-os ~/ai-os ~/Downloads/ai-os /tmp/ai-os; do
  if [ -d "$dir" ]; then
    rm -rf "$dir" && log "Removed AI-OS directory: $dir" || warn "Could not remove $dir"
  fi
done

log "Removing shell integration from user shell config files..."
# List of shell config files to check
shell_configs=(
    ~/.bashrc
    ~/.bash_profile
    ~/.bash_login
    ~/.profile
    ~/.zshrc
    ~/.zprofile
    ~/.zshenv
    ~/.config/fish/config.fish
    ~/.config/fish/fish.config
)

for shellrc in "${shell_configs[@]}"; do
  if [ -f "$shellrc" ]; then
    # Check for various AI-OS integration patterns
    removed_lines=0
    
    # Remove ai-shell.sh source lines
    if grep -q 'ai-shell.sh' "$shellrc"; then
      sed -i '/ai-shell.sh/d' "$shellrc"
      removed_lines=$((removed_lines + $(grep -c 'ai-shell.sh' "$shellrc" 2>/dev/null || echo 0)))
      log "Removed ai-shell.sh integration from $shellrc"
    fi
    
    # Remove direct-ai-shell.sh source lines
    if grep -q 'direct-ai-shell.sh' "$shellrc"; then
      sed -i '/direct-ai-shell.sh/d' "$shellrc"
      removed_lines=$((removed_lines + $(grep -c 'direct-ai-shell.sh' "$shellrc" 2>/dev/null || echo 0)))
      log "Removed direct-ai-shell.sh integration from $shellrc"
    fi
    
    # Remove ultimate-ai-shell.sh source lines
    if grep -q 'ultimate-ai-shell.sh' "$shellrc"; then
      sed -i '/ultimate-ai-shell.sh/d' "$shellrc"
      removed_lines=$((removed_lines + $(grep -c 'ultimate-ai-shell.sh' "$shellrc" 2>/dev/null || echo 0)))
      log "Removed ultimate-ai-shell.sh integration from $shellrc"
    fi
    
    # Remove AI-OS related comments and lines
    if grep -q 'AI-OS' "$shellrc"; then
      sed -i '/# AI-OS/d' "$shellrc"
      sed -i '/AI-OS Shell Integration/d' "$shellrc"
      sed -i '/AI-OS:/d' "$shellrc"
      log "Removed AI-OS comments from $shellrc"
    fi
    
    # Remove any lines containing ai-os (case insensitive)
    if grep -qi 'ai-os' "$shellrc"; then
      sed -i '/ai-os/I d' "$shellrc"
      log "Removed any remaining ai-os references from $shellrc"
    fi
    
    if [ $removed_lines -gt 0 ]; then
      log "Removed $removed_lines AI-OS integration lines from $shellrc"
    fi
  fi
done

log "Cleaning up any remaining AI-OS files..."
# Remove any remaining AI-OS related files
find /tmp -name "*ai-os*" -type f -delete 2>/dev/null || true
find /tmp -name "*ai_os*" -type f -delete 2>/dev/null || true

log "Reloading systemd daemon..."
sudo systemctl daemon-reload

log "Clearing shell command cache..."
hash -r

log "AI-OS has been fully uninstalled."
echo ""
echo "Note: If you have AI-OS functions loaded in your current shell session,"
echo "you may need to restart your terminal or run 'exec bash' to clear them."
echo ""
echo "To verify removal, check that these files no longer exist:"
echo "  - /usr/local/sbin/ai-os-daemon"
echo "  - /usr/local/bin/ai-client"
echo "  - /etc/ai-os/"
echo "  - /var/log/ai-os/"
echo "  - ~/Desktop/ai-os/ (or other AI-OS directories)"
echo ""
echo "To completely clear your current shell session, run:"
echo "  exec bash" 