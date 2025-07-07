# AI-OS

AI-OS is a natural language shell assistant for Linux. Type commands in plain English and let AI-OS interpret and execute them for you!

## Features

- Natural language command interpretation
- Seamless shell integration (no need to type `ai`)
- Systemd-managed daemon
- Easy install/uninstall

## Installation

### 1. Install Dependencies

**Arch Linux:**
```bash
sudo pacman -Syu --noconfirm
sudo pacman -S --noconfirm base-devel linux-headers dbus json-c curl cmake git wget python systemd dkms gnu-netcat lsof
```

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install -y build-essential linux-headers-\$(uname -r) libdbus-1-dev libjson-c-dev libcurl4-openssl-dev cmake git wget curl python3-dev pkg-config systemd dkms
```

### 2. Install Ollama

```bash
curl -fsSL https://ollama.ai/install.sh | sh
ollama serve &
ollama pull phi3:mini
ollama pull codellama:7b-instruct
```

### 3. Build AI-OS

```bash
git clone https://github.com/saquib34/ai-os.git
cd ai-os
make clean
make all
```

### 4. Install Binaries and Shell Integration

```bash
sudo cp build/ai-os-daemon /usr/local/sbin/
sudo cp build/ai-client /usr/local/bin/
sudo chmod +x /usr/local/sbin/ai-os-daemon /usr/local/bin/ai-client

sudo mkdir -p /usr/local/share/ai-os/shell
sudo cp userspace/shell-integration/ai-shell.sh /usr/local/share/ai-os/shell/
sudo chmod +x /usr/local/share/ai-os/shell/ai-shell.sh
echo source /usr/local/share/ai-os/shell/ai-shell.sh >> ~/.bashrc
source ~/.bashrc
```

### 5. Set Up and Start the Daemon

```bash
sudo tee /etc/systemd/system/ai-os.service > /dev/null <<EOF
[Unit]
Description=AI Operating System Daemon
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/sbin/ai-os-daemon
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable ai-os
sudo systemctl start ai-os
```

## Usage

- Just type natural language commands in your terminal (e.g., `install git`, `list all files`).
- To run a raw shell command, prefix it with a space (e.g., ` echo hello`).

## Uninstall

```bash
sudo systemctl stop ai-os
sudo systemctl disable ai-os
sudo rm -f /usr/local/sbin/ai-os-daemon /usr/local/bin/ai-client
sudo rm -rf /usr/local/share/ai-os
sudo rm -f /etc/systemd/system/ai-os.service
sudo rm -rf /etc/ai-os /var/log/ai-os.log
sed -i /ai-shell.sh/d ~/.bashrc
hash -r
```

## License

MIT or your preferred license.

