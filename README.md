# AI-OS

AI-OS is a natural language shell assistant for Linux. Type commands in plain English and let AI-OS interpret and execute them for you!

## Features

- Natural language command interpretation
- Seamless shell integration (no need to type `ai`)
- Systemd-managed daemon
- Easy install/uninstall

## Installation

**All you need is the installer script!**

```bash
# Clone the repository (if you haven't already)
git clone https://github.com/saquib34/ai-os.git
cd ai-os

# Run the installer (it will handle everything for you)
./scripts/install-ai-os.sh
```

- The installer will:
  - Detect your Linux distribution and install all dependencies
  - Install and configure Ollama (AI backend)
  - Automatically select the best model for your available RAM (uses a smaller model if you have less than 6GB)
  - Build and install all AI-OS components
  - Set up config files, permissions, logging, and shell integration
  - Start the daemon and verify everything is working

**No need to manually install dependencies, models, or configure anything!**

## Usage

- Just type natural language commands in your terminal (e.g., `install git`, `list all files`).
- To run a raw shell command, prefix it with a space (e.g., ` echo hello`).

## Uninstall

You can uninstall AI-OS using the script:

```bash
./scripts/install-ai-os.sh uninstall
```

Or manually:
```bash
sudo systemctl stop ai-os
sudo systemctl disable ai-os
sudo rm -f /usr/local/sbin/ai-os-daemon /usr/local/bin/ai-client
sudo rm -rf /usr/local/share/ai-os
sudo rm -f /etc/systemd/system/ai-os.service
sudo rm -rf /etc/ai-os /var/log/ai-os
sudo rm -f /lib/modules/$(uname -r)/extra/ai_os.ko
sudo rm -f /etc/logrotate.d/ai-os
sed -i /ai-shell.sh/d ~/.bashrc
sudo systemctl daemon-reload
hash -r
```

## License

MIT or your preferred license.

