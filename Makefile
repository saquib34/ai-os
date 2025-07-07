# AI-OS Makefile
# Build system for AI Operating System Integration

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE
LDFLAGS = -lcurl -ljson-c -lpthread -ldbus-1
INSTALL_PREFIX = /usr/local
SYSTEMD_DIR = /etc/systemd/system
CONFIG_DIR = /etc/ai-os

# Directories
KERNEL_DIR = kernel
USERSPACE_DIR = userspace
CLIENT_DIR = $(USERSPACE_DIR)/client
DAEMON_DIR = $(USERSPACE_DIR)/daemon
SHELL_DIR = $(USERSPACE_DIR)/shell-integration
BUILD_DIR = build
INSTALL_DIR = $(INSTALL_PREFIX)

# Source files
OLLAMA_CLIENT_SRC = $(CLIENT_DIR)/ollama_client.c
CONTEXT_MANAGER_SRC = $(DAEMON_DIR)/context_manager.c
AI_DAEMON_SRC = $(DAEMON_DIR)/ai_daemon.c
CLIENT_LIB_SRC = $(CLIENT_DIR)/ai_client.c
CLI_CLIENT_SRC = $(CLIENT_DIR)/ai-client.c

# Object files
OLLAMA_CLIENT_OBJ = $(BUILD_DIR)/ollama_client.o
CONTEXT_MANAGER_OBJ = $(BUILD_DIR)/context_manager.o
AI_DAEMON_OBJ = $(BUILD_DIR)/ai_daemon.o
CLIENT_LIB_OBJ = $(BUILD_DIR)/ai_client.o
CLI_CLIENT_OBJ = $(BUILD_DIR)/ai-client.o

# Targets
DAEMON_TARGET = $(BUILD_DIR)/ai-os-daemon
CLIENT_TARGET = $(BUILD_DIR)/ai-client
KERNEL_MODULE = $(BUILD_DIR)/ai_os.ko

# Default target
.PHONY: all clean install uninstall kernel userspace daemon client shell-integration

all: userspace

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Userspace targets
userspace: daemon client

# Compile individual object files
$(OLLAMA_CLIENT_OBJ): $(OLLAMA_CLIENT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(CONTEXT_MANAGER_OBJ): $(CONTEXT_MANAGER_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(AI_DAEMON_OBJ): $(AI_DAEMON_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(CLIENT_LIB_OBJ): $(CLIENT_LIB_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(CLI_CLIENT_OBJ): $(CLI_CLIENT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Build daemon
daemon: $(DAEMON_TARGET)

$(DAEMON_TARGET): $(OLLAMA_CLIENT_OBJ) $(CONTEXT_MANAGER_OBJ) $(AI_DAEMON_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Build client
client: $(CLIENT_TARGET)

$(CLIENT_TARGET): $(CLIENT_LIB_OBJ) $(CLI_CLIENT_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Build kernel module
kernel: $(KERNEL_MODULE)

$(KERNEL_MODULE): $(KERNEL_DIR)/ai_os.c | $(BUILD_DIR)
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD)/$(KERNEL_DIR) modules
	cp $(KERNEL_DIR)/ai_os.ko $(BUILD_DIR)/

# Install targets
install: install-userspace install-config install-systemd

install-userspace: userspace
	@echo "Installing AI-OS userspace components..."
	sudo mkdir -p $(INSTALL_DIR)/bin
	sudo mkdir -p $(INSTALL_DIR)/sbin
	sudo cp $(DAEMON_TARGET) $(INSTALL_DIR)/sbin/
	sudo cp $(CLIENT_TARGET) $(INSTALL_DIR)/bin/
	sudo chmod +x $(INSTALL_DIR)/sbin/ai-os-daemon
	sudo chmod +x $(INSTALL_DIR)/bin/ai-client
	@echo "Userspace components installed."

install-kernel: kernel
	@echo "Installing AI-OS kernel module..."
	sudo mkdir -p /lib/modules/$(shell uname -r)/extra
	sudo cp $(KERNEL_MODULE) /lib/modules/$(shell uname -r)/extra/
	sudo depmod -a
	@echo "Kernel module installed."

install-config:
	@echo "Installing configuration files..."
	sudo mkdir -p $(CONFIG_DIR)
	sudo mkdir -p $(CONFIG_DIR)/models
	echo '{"model": "codellama:7b-instruct", "safety_mode": true, "confirmation_required": true}' | sudo tee $(CONFIG_DIR)/config.json > /dev/null
	sudo chmod 644 $(CONFIG_DIR)/config.json
	@echo "Configuration files installed."

install-systemd:
	@echo "Installing systemd service..."
	@echo "[Unit]" | sudo tee $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "Description=AI Operating System Daemon" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "After=network.target" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "[Service]" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "Type=simple" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "ExecStart=$(INSTALL_PREFIX)/sbin/ai-os-daemon" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "Restart=always" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "RestartSec=5" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "[Install]" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	@echo "WantedBy=multi-user.target" | sudo tee -a $(SYSTEMD_DIR)/ai-os.service > /dev/null
	sudo systemctl daemon-reload
	@echo "Systemd service installed."
	@echo "Systemd service installed."

install-shell-integration:
	@echo "Installing shell integration..."
	sudo mkdir -p $(INSTALL_PREFIX)/share/ai-os/shell
	sudo cp $(SHELL_DIR)/ai-shell.sh $(INSTALL_PREFIX)/share/ai-os/shell/
	sudo chmod +x $(INSTALL_PREFIX)/share/ai-os/shell/ai-shell.sh
	@echo ""
	@echo "Shell integration installed. Add this line to your ~/.bashrc or ~/.zshrc:"
	@echo "source $(INSTALL_PREFIX)/share/ai-os/shell/ai-shell.sh"
	@echo ""

# Setup targets
setup-ollama:
	@echo "Setting up Ollama..."
	@if ! command -v ollama >/dev/null 2>&1; then \
		echo "Installing Ollama..."; \
		curl -fsSL https://ollama.ai/install.sh | sh; \
	else \
		echo "Ollama already installed."; \
	fi
	@echo "Pulling AI models..."
	ollama pull codellama:7b-instruct
	ollama pull phi3:mini
	@echo "Ollama setup complete."

setup-dependencies:
	@echo "Installing dependencies..."
	sudo apt update
	sudo apt install -y build-essential linux-headers-$(shell uname -r) \
		libdbus-1-dev libjson-c-dev libcurl4-openssl-dev \
		cmake git wget curl python3-dev
	@echo "Dependencies installed."

setup: setup-dependencies setup-ollama

# Start/stop services
start:
	@echo "Starting AI-OS services..."
	sudo systemctl start ai-os
	sudo systemctl status ai-os --no-pager

stop:
	@echo "Stopping AI-OS services..."
	sudo systemctl stop ai-os

restart:
	@echo "Restarting AI-OS services..."
	sudo systemctl restart ai-os
	sudo systemctl status ai-os --no-pager

enable:
	@echo "Enabling AI-OS service..."
	sudo systemctl enable ai-os

disable:
	@echo "Disabling AI-OS service..."
	sudo systemctl disable ai-os

# Load/unload kernel module
load-kernel:
	sudo modprobe ai_os

unload-kernel:
	sudo modprobe -r ai_os

# Testing targets
test-daemon:
	@echo "Testing AI daemon connection..."
	$(CLIENT_TARGET) status

test-interpretation:
	@echo "Testing command interpretation..."
	$(CLIENT_TARGET) interpret "list files in current directory"
	$(CLIENT_TARGET) interpret "git push and add all files"
	$(CLIENT_TARGET) interpret "install python package requests"

test: test-daemon test-interpretation

# Development targets
dev-install: all
	sudo cp $(DAEMON_TARGET) $(INSTALL_DIR)/sbin/
	sudo cp $(CLIENT_TARGET) $(INSTALL_DIR)/bin/
	sudo systemctl restart ai-os 2>/dev/null || true

dev-logs:
	sudo journalctl -u ai-os -f

dev-debug: all
	sudo gdb --args $(DAEMON_TARGET)

# Clean targets
clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C $(KERNEL_DIR) clean 2>/dev/null || true

clean-all: clean
	sudo systemctl stop ai-os 2>/dev/null || true
	sudo systemctl disable ai-os 2>/dev/null || true

# Uninstall targets
uninstall:
	@echo "Uninstalling AI-OS..."
	sudo systemctl stop ai-os 2>/dev/null || true
	sudo systemctl disable ai-os 2>/dev/null || true
	sudo rm -f $(SYSTEMD_DIR)/ai-os.service
	sudo rm -f $(INSTALL_DIR)/sbin/ai-os-daemon
	sudo rm -f $(INSTALL_DIR)/bin/ai-client
	sudo rm -rf $(INSTALL_PREFIX)/share/ai-os
	sudo rm -rf $(CONFIG_DIR)
	sudo rm -f /lib/modules/$(shell uname -r)/extra/ai_os.ko
	sudo depmod -a
	sudo systemctl daemon-reload
	@echo "AI-OS uninstalled."

# Documentation targets
docs:
	@echo "Generating documentation..."
	mkdir -p docs/html
	@echo "# AI-OS Documentation" > docs/README.md
	@echo "" >> docs/README.md
	@echo "## Installation" >> docs/README.md
	@echo "\`\`\`bash" >> docs/README.md
	@echo "make setup" >> docs/README.md
	@echo "make install" >> docs/README.md
	@echo "make start" >> docs/README.md
	@echo "\`\`\`" >> docs/README.md
	@echo "" >> docs/README.md
	@echo "## Usage" >> docs/README.md
	@echo "\`\`\`bash" >> docs/README.md
	@echo "ai-client interpret \"git push and add all files\"" >> docs/README.md
	@echo "ai \"install python package numpy\"" >> docs/README.md
	@echo "\`\`\`" >> docs/README.md
	@echo "Documentation generated in docs/"

# Package targets
package:
	@echo "Creating package..."
	mkdir -p ai-os-package
	cp -r $(USERSPACE_DIR) ai-os-package/
	cp -r $(KERNEL_DIR) ai-os-package/
	cp Makefile ai-os-package/
	cp README.md ai-os-package/ 2>/dev/null || echo "# AI-OS Package" > ai-os-package/README.md
	tar -czf ai-os-$(shell date +%Y%m%d).tar.gz ai-os-package/
	rm -rf ai-os-package/
	@echo "Package created: ai-os-$(shell date +%Y%m%d).tar.gz"

# Check system requirements
check-system:
	@echo "Checking system requirements..."
	@echo -n "Kernel headers: "
	@ls /lib/modules/$(shell uname -r)/build >/dev/null 2>&1 && echo "✓" || echo "✗ (install linux-headers)"
	@echo -n "libcurl: "
	@pkg-config --exists libcurl && echo "✓" || echo "✗ (install libcurl4-openssl-dev)"
	@echo -n "json-c: "
	@pkg-config --exists json-c && echo "✓" || echo "✗ (install libjson-c-dev)"
	@echo -n "dbus: "
	@pkg-config --exists dbus-1 && echo "✓" || echo "✗ (install libdbus-1-dev)"
	@echo -n "Ollama: "
	@command -v ollama >/dev/null 2>&1 && echo "✓" || echo "✗ (run make setup-ollama)"

# Show status
status:
	@echo "AI-OS Status:"
	@echo "============="
	@echo -n "Daemon: "
	@systemctl is-active ai-os 2>/dev/null || echo "inactive"
	@echo -n "Kernel module: "
	@lsmod | grep -q ai_os && echo "loaded" || echo "not loaded"
	@echo -n "Ollama: "
	@pgrep ollama >/dev/null && echo "running" || echo "not running"
	@echo ""
	@echo "Config: $(CONFIG_DIR)/config.json"
	@echo "Logs: sudo journalctl -u ai-os"
	@echo "Socket: /var/run/ai-os.sock"

# Help target
help:
	@echo "AI-OS Build System"
	@echo "=================="
	@echo ""
	@echo "Setup:"
	@echo "  make setup              - Install dependencies and Ollama"
	@echo "  make setup-dependencies - Install system dependencies"
	@echo "  make setup-ollama      - Install and configure Ollama"
	@echo ""
	@echo "Build:"
	@echo "  make all               - Build all userspace components"
	@echo "  make userspace         - Build userspace components only"
	@echo "  make daemon            - Build AI daemon"
	@echo "  make client            - Build CLI client"
	@echo "  make kernel            - Build kernel module"
	@echo ""
	@echo "Install:"
	@echo "  make install           - Install all components"
	@echo "  make install-userspace - Install userspace components"
	@echo "  make install-kernel    - Install kernel module"
	@echo "  make install-shell-integration - Install shell integration"
	@echo ""
	@echo "Service Management:"
	@echo "  make start             - Start AI-OS daemon"
	@echo "  make stop              - Stop AI-OS daemon"
	@echo "  make restart           - Restart AI-OS daemon"
	@echo "  make enable            - Enable AI-OS service"
	@echo "  make disable           - Disable AI-OS service"
	@echo ""
	@echo "Testing:"
	@echo "  make test              - Run basic tests"
	@echo "  make test-daemon       - Test daemon connection"
	@echo "  make test-interpretation - Test command interpretation"
	@echo ""
	@echo "Development:"
	@echo "  make dev-install       - Quick install for development"
	@echo "  make dev-logs          - Follow daemon logs"
	@echo "  make dev-debug         - Debug daemon with GDB"
	@echo ""
	@echo "Maintenance:"
	@echo "  make status            - Show system status"
	@echo "  make check-system      - Check system requirements"
	@echo "  make clean             - Clean build files"
	@echo "  make uninstall         - Uninstall AI-OS"
	@echo ""
	@echo "Documentation:"
	@echo "  make docs              - Generate documentation"
	@echo "  make package           - Create distribution package"