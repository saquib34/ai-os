#!/bin/bash
# Complete AI-OS Integration and Deployment Script
# File: scripts/install-ai-os.sh

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
AI_OS_VERSION="1.0.0"
INSTALL_PREFIX="/usr/local"
CONFIG_DIR="/etc/ai-os"
LOG_FILE="/tmp/ai-os-install.log"
BACKUP_DIR="/tmp/ai-os-backup-$(date +%Y%m%d-%H%M%S)"

# Functions
log() {
    echo -e "${GREEN}[$(date '+%Y-%m-%d %H:%M:%S')] $1${NC}" | tee -a "$LOG_FILE"
}

warn() {
    echo -e "${YELLOW}[WARNING] $1${NC}" | tee -a "$LOG_FILE"
}

error() {
    echo -e "${RED}[ERROR] $1${NC}" | tee -a "$LOG_FILE"
}

info() {
    echo -e "${BLUE}[INFO] $1${NC}" | tee -a "$LOG_FILE"
}

# Check if running as root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        warn "Running as root. This is not recommended for development."
        read -p "Continue anyway? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
}

# Check system requirements
check_requirements() {
    log "Checking system requirements..."
    
    # Check OS
    if ! command -v apt >/dev/null 2>&1; then
        error "This script requires a Debian/Ubuntu-based system"
        exit 1
    fi
    
    # Check kernel version
    KERNEL_VERSION=$(uname -r)
    KERNEL_MAJOR=$(echo "$KERNEL_VERSION" | cut -d. -f1)
    KERNEL_MINOR=$(echo "$KERNEL_VERSION" | cut -d. -f2)
    
    if [[ $KERNEL_MAJOR -lt 5 ]] || [[ $KERNEL_MAJOR -eq 5 && $KERNEL_MINOR -lt 4 ]]; then
        error "Kernel version 5.4+ required. Current: $KERNEL_VERSION"
        exit 1
    fi
    
    # Check available memory
    TOTAL_MEM=$(free -m | awk 'NR==2{print $2}')
    if [[ $TOTAL_MEM -lt 4096 ]]; then
        warn "Low memory detected: ${TOTAL_MEM}MB. Recommended: 4GB+"
    fi
    
    # Check disk space
    AVAILABLE_SPACE=$(df / | awk 'NR==2 {print $4}')
    if [[ $AVAILABLE_SPACE -lt 10485760 ]]; then  # 10GB in KB
        warn "Low disk space. Recommended: 10GB+ free"
    fi
    
    log "System requirements check completed"
}

# Install system dependencies
install_dependencies() {
    log "Installing system dependencies..."
    
    sudo apt update
    sudo apt install -y \
        build-essential \
        linux-headers-$(uname -r) \
        libdbus-1-dev \
        libjson-c-dev \
        libcurl4-openssl-dev \
        cmake \
        git \
        wget \
        curl \
        python3-dev \
        pkg-config \
        systemd \
        dkms \
        module-assistant \
        debhelper \
        || { error "Failed to install dependencies"; exit 1; }
    
    log "Dependencies installed successfully"
}

# Install Ollama
install_ollama() {
    log "Installing Ollama..."
    
    if command -v ollama >/dev/null 2>&1; then
        info "Ollama already installed"
        ollama --version
    else
        curl -fsSL https://ollama.ai/install.sh | sh || {
            error "Failed to install Ollama"
            exit 1
        }
    fi
    
    # Start Ollama service
    if ! pgrep ollama >/dev/null; then
        log "Starting Ollama service..."
        ollama serve &
        sleep 5
    fi
    
    # Pull required models
    log "Downloading AI models (this may take several minutes)..."
    ollama pull codellama:7b-instruct || warn "Failed to pull codellama model"
    ollama pull phi3:mini || warn "Failed to pull phi3 model"
    
    # Verify models
    log "Available models:"
    ollama list
}

# Create directory structure
create_directories() {
    log "Creating directory structure..."
    
    # Create source directories
    mkdir -p ai-os/{kernel,userspace/{daemon,client,shell-integration},config,build,scripts}
    
    # Create system directories
    sudo mkdir -p "$CONFIG_DIR"
    sudo mkdir -p "$INSTALL_PREFIX/share/ai-os/shell"
    sudo mkdir -p /var/log/ai-os
    sudo mkdir -p /lib/modules/$(uname -r)/extra
    
    log "Directory structure created"
}

# Install source files
install_source_files() {
    log "Installing source files..."
    
    # Create header file for shared definitions
    cat > userspace/ai_os_common.h << 'EOF'
#ifndef AI_OS_COMMON_H
#define AI_OS_COMMON_H

#include <time.h>

/* Process context structure */
typedef struct {
    char current_directory[1024];
    char username[64];
    char shell[64];
    char hostname[64];
    char git_branch[128];
    char git_status[256];
    char recent_commands[50][256];
    int command_count;
    char file_listing[1024];
    char system_info[512];
    time_t last_update;
    pid_t process_id;
    uid_t user_id;
} ai_context_t;

/* Function declarations */
int ai_client_connect(void);
void ai_client_disconnect(void);
int ai_interpret_command(const char *natural_command, char *shell_command, size_t command_size);
int ai_execute_command(const char *command, char *output, size_t output_size);
int ai_get_status(char *status_info, size_t info_size);
int ai_set_model(const char *model_name);
int ai_get_context(char *context_info, size_t info_size);

int ollama_client_init(const char *model_name, const char *api_url);
int ollama_interpret_command(const char *natural_command, const char *context, 
                            char *shell_command, size_t command_size);
int ollama_check_status(void);
int ollama_list_models(char *models_list, size_t list_size);
int ollama_set_model(const char *model_name);
void ollama_client_cleanup(void);

int ai_context_create(ai_context_t *ctx, pid_t pid);
char *ai_context_to_json(const ai_context_t *ctx);
char *ai_context_to_summary(const ai_context_t *ctx);
int ai_context_update(ai_context_t *ctx);
int ai_context_add_command(ai_context_t *ctx, const char *command);
int ai_context_needs_refresh(ai_context_t *ctx);
void ai_context_free(ai_context_t *ctx);

int kernel_bridge_init(void);
int kernel_bridge_start(void);
void kernel_bridge_stop(void);
void kernel_bridge_cleanup(void);

#endif /* AI_OS_COMMON_H */
EOF

    log "Source files prepared"
}

# Build AI-OS components
build_components() {
    log "Building AI-OS components..."
    
    # Build userspace components
    log "Building userspace components..."
    make clean 2>/dev/null || true
    make all || { error "Failed to build userspace components"; exit 1; }
    
    # Build kernel module
    log "Building kernel module..."
    cd kernel
    make clean 2>/dev/null || true
    make all || { error "Failed to build kernel module"; exit 1; }
    cd ..
    
    log "Build completed successfully"
}

# Install components
install_components() {
    log "Installing AI-OS components..."
    
    # Install userspace components
    make install-userspace || { error "Failed to install userspace components"; exit 1; }
    
    # Install kernel module
    make install-kernel || { error "Failed to install kernel module"; exit 1; }
    
    # Install configuration
    make install-config || { error "Failed to install configuration"; exit 1; }
    
    # Install systemd service
    make install-systemd || { error "Failed to install systemd service"; exit 1; }
    
    # Install shell integration
    make install-shell-integration || { error "Failed to install shell integration"; exit 1; }
    
    log "Components installed successfully"
}

# Configure system
configure_system() {
    log "Configuring AI-OS system..."
    
    # Set up configuration file
    sudo tee "$CONFIG_DIR/config.json" > /dev/null << EOF
{
    "model": "codellama:7b-instruct",
    "safety_mode": true,
    "confirmation_required": true,
    "api_url": "http://localhost:11434/api",
    "timeout": 30,
    "max_tokens": 512,
    "temperature": 0.1,
    "kernel_integration": true,
    "debug_mode": false
}
EOF
    
    # Set permissions
    sudo chmod 644 "$CONFIG_DIR/config.json"
    sudo chown root:root "$CONFIG_DIR/config.json"
    
    # Create log rotation
    sudo tee /etc/logrotate.d/ai-os > /dev/null << EOF
/var/log/ai-os.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    create 644 root root
}
EOF
    
    log "System configuration completed"
}

# Start services
start_services() {
    log "Starting AI-OS services..."
    
    # Load kernel module
    if ! lsmod | grep -q ai_os; then
        sudo modprobe ai_os || warn "Failed to load kernel module"
    fi
    
    # Enable and start daemon
    sudo systemctl daemon-reload
    sudo systemctl enable ai-os
    sudo systemctl start ai-os
    
    # Wait for service to start
    sleep 3
    
    # Check status
    if sudo systemctl is-active --quiet ai-os; then
        log "AI-OS daemon started successfully"
    else
        error "Failed to start AI-OS daemon"
        sudo systemctl status ai-os
        exit 1
    fi
    
    log "Services started successfully"
}

# Run tests
run_tests() {
    log "Running system tests..."
    
    # Test daemon connectivity
    if ai-client status >/dev/null 2>&1; then
        log "✓ Daemon connectivity test passed"
    else
        error "✗ Daemon connectivity test failed"
        return 1
    fi
    
    # Test command interpretation
    local test_result
    test_result=$(ai-client interpret "list files in current directory" 2>/dev/null)
    if [[ "$test_result" == "ls -la" ]] || [[ "$test_result" == "ls" ]]; then
        log "✓ Command interpretation test passed"
    else
        warn "✗ Command interpretation test failed or gave unexpected result: $test_result"
    fi
    
    # Test Ollama connectivity
    if ai-client status 2>/dev/null | grep -q "ollama_status.*running"; then
        log "✓ Ollama connectivity test passed"
    else
        warn "✗ Ollama connectivity test failed"
    fi
    
    # Test kernel module
    if lsmod | grep -q ai_os; then
        log "✓ Kernel module test passed"
    else
        warn "✗ Kernel module test failed"
    fi
    
    log "System tests completed"
}

# Setup shell integration
setup_shell_integration() {
    log "Setting up shell integration..."
    
    local shell_config=""
    local current_shell=$(basename "$SHELL")
    
    case "$current_shell" in
        bash)
            shell_config="$HOME/.bashrc"
            ;;
        zsh)
            shell_config="$HOME/.zshrc"
            ;;
        fish)
            shell_config="$HOME/.config/fish/config.fish"
            ;;
        *)
            warn "Unsupported shell: $current_shell. Manual setup required."
            return 0
            ;;
    esac
    
    if [[ -f "$shell_config" ]]; then
        # Check if already configured
        if grep -q "ai-shell.sh" "$shell_config"; then
            info "Shell integration already configured in $shell_config"
        else
            # Add source line
            echo "" >> "$shell_config"
            echo "# AI-OS Shell Integration" >> "$shell_config"
            echo "source $INSTALL_PREFIX/share/ai-os/shell/ai-shell.sh" >> "$shell_config"
            log "Shell integration added to $shell_config"
            
            # Suggest reloading
            info "Run 'source $shell_config' to activate shell integration"
        fi
    else
        warn "Shell config file not found: $shell_config"
    fi
    
    log "Shell integration setup completed"
}

# Create backup
create_backup() {
    log "Creating backup of existing installation..."
    
    mkdir -p "$BACKUP_DIR"
    
    # Backup existing files
    [[ -f "$INSTALL_PREFIX/sbin/ai-os-daemon" ]] && cp "$INSTALL_PREFIX/sbin/ai-os-daemon" "$BACKUP_DIR/"
    [[ -f "$INSTALL_PREFIX/bin/ai-client" ]] && cp "$INSTALL_PREFIX/bin/ai-client" "$BACKUP_DIR/"
    [[ -d "$CONFIG_DIR" ]] && cp -r "$CONFIG_DIR" "$BACKUP_DIR/"
    [[ -f "/etc/systemd/system/ai-os.service" ]] && cp "/etc/systemd/system/ai-os.service" "$BACKUP_DIR/"
    
    log "Backup created at: $BACKUP_DIR"
}

# Show system status
show_status() {
    log "AI-OS System Status:"
    echo "===================="
    
    # Daemon status
    if sudo systemctl is-active --quiet ai-os; then
        echo -e "${GREEN}✓ Daemon: Running${NC}"
    else
        echo -e "${RED}✗ Daemon: Stopped${NC}"
    fi
    
    # Kernel module status
    if lsmod | grep -q ai_os; then
        echo -e "${GREEN}✓ Kernel Module: Loaded${NC}"
    else
        echo -e "${RED}✗ Kernel Module: Not loaded${NC}"
    fi
    
    # Ollama status
    if pgrep ollama >/dev/null; then
        echo -e "${GREEN}✓ Ollama: Running${NC}"
    else
        echo -e "${RED}✗ Ollama: Not running${NC}"
    fi
    
    # Configuration
    echo -e "${BLUE}Configuration: $CONFIG_DIR/config.json${NC}"
    echo -e "${BLUE}Logs: sudo journalctl -u ai-os${NC}"
    echo -e "${BLUE}Socket: /var/run/ai-os.sock${NC}"
    
    # Quick test
    echo ""
    echo "Quick Test:"
    if command -v ai-client >/dev/null 2>&1; then
        ai-client interpret "show files" 2>/dev/null || echo "Interpretation test failed"
    else
        echo "ai-client not found in PATH"
    fi
}

# Uninstall function
uninstall_ai_os() {
    log "Uninstalling AI-OS..."
    
    # Stop services
    sudo systemctl stop ai-os 2>/dev/null || true
    sudo systemctl disable ai-os 2>/dev/null || true
    
    # Unload kernel module
    sudo rmmod ai_os 2>/dev/null || true
    
    # Remove files
    sudo rm -f "$INSTALL_PREFIX/sbin/ai-os-daemon"
    sudo rm -f "$INSTALL_PREFIX/bin/ai-client"
    sudo rm -f "/etc/systemd/system/ai-os.service"
    sudo rm -rf "$INSTALL_PREFIX/share/ai-os"
    sudo rm -rf "$CONFIG_DIR"
    sudo rm -f "/lib/modules/$(uname -r)/extra/ai_os.ko"
    sudo rm -f "/etc/logrotate.d/ai-os"
    
    # Update module dependencies
    sudo depmod -a
    sudo systemctl daemon-reload
    
    log "AI-OS uninstalled successfully"
}

# Main installation function
main_install() {
    log "Starting AI-OS Installation v$AI_OS_VERSION"
    
    # Pre-installation checks
    check_root
    check_requirements
    
    # Create backup if existing installation
    if [[ -f "$INSTALL_PREFIX/sbin/ai-os-daemon" ]]; then
        create_backup
    fi
    
    # Installation steps
    install_dependencies
    install_ollama
    create_directories
    install_source_files
    build_components
    install_components
    configure_system
    start_services
    setup_shell_integration
    
    # Post-installation
    run_tests
    show_status
    
    log "AI-OS installation completed successfully!"
    echo ""
    echo "Next steps:"
    echo "1. Reload your shell: source ~/.bashrc (or ~/.zshrc)"
    echo "2. Try: ai \"list files in current directory\""
    echo "3. Check status: ai-status"
    echo "4. Get help: ai-help"
    echo ""
    echo "For more information, see the documentation."
}

# Parse command line arguments
case "${1:-install}" in
    install)
        main_install
        ;;
    uninstall)
        uninstall_ai_os
        ;;
    status)
        show_status
        ;;
    test)
        run_tests
        ;;
    backup)
        create_backup
        ;;
    update)
        log "Updating AI-OS..."
        create_backup
        build_components
        install_components
        sudo systemctl restart ai-os
        log "Update completed"
        ;;
    clean)
        log "Cleaning build files..."
        make clean 2>/dev/null || true
        cd kernel && make clean 2>/dev/null || true
        rm -rf build/
        log "Clean completed"
        ;;
    help|--help|-h)
        echo "AI-OS Installation Script v$AI_OS_VERSION"
        echo ""
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  install   - Full installation (default)"
        echo "  uninstall - Remove AI-OS completely"
        echo "  status    - Show system status"
        echo "  test      - Run system tests"
        echo "  backup    - Create backup of current installation"
        echo "  update    - Update existing installation"
        echo "  clean     - Clean build files"
        echo "  help      - Show this help"
        echo ""
        echo "Examples:"
        echo "  $0                    # Full installation"
        echo "  $0 install           # Full installation"
        echo "  $0 status            # Show status"
        echo "  $0 uninstall         # Remove everything"
        echo ""
        ;;
    *)
        error "Unknown command: $1"
        echo "Use '$0 help' for usage information"
        exit 1
        ;;
esac