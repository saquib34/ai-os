# AI-OS: Advanced Natural Language Shell Integration

AI-OS is a comprehensive natural language processing system for Linux that transforms your shell into an intelligent, context-aware assistant. Type commands in plain English and let AI-OS interpret and execute them for you!

## 🚀 Core Features

### **Natural Language Command Interpretation**
- **Zero-prefix operation**: Type natural language directly without `ai` prefix
- **Multi-stage classification**: Jaccard index pattern matching + AI fallback
- **Persistent learning**: Automatically saves and learns from new patterns
- **Context awareness**: Considers current directory, git status, system state
- **Safety controls**: Multiple confirmation modes and dangerous command blocking

### **Advanced Shell Integration**
- **Three integration modes**:
  - `ai-shell.sh`: Two-stage classification with pattern learning
  - `direct-ai-shell.sh`: Direct natural language interpretation
  - `ultimate-ai-shell.sh`: Machine learning with 15+ intent categories
- **Command not found handling**: Automatic interpretation of unknown commands
- **Tab completion**: Smart suggestions for natural language commands
- **History integration**: Commands added to shell history

### **Intelligent Classification System**
- **Command vs Chat detection**: Automatically distinguishes between shell commands and conversational input
- **Pattern-based learning**: Saves successful interpretations to `~/.ai-os-command-patterns.txt` and `~/.ai-os-chat-patterns.txt`
- **Jaccard similarity scoring**: Fast pattern matching against learned examples
- **AI fallback**: Uses `ai-client classify` for ambiguous inputs

### **Kernel-Level Integration**
- **Linux kernel module**: Advanced system call interception and context gathering
- **Character device interface**: `/dev/ai_os` for kernel-userspace communication
- **Netlink socket**: Real-time kernel event processing
- **Context tracking**: Process, user, and system state monitoring

### **Daemon Architecture**
- **Systemd service**: Managed daemon with automatic startup
- **Multi-client support**: Handles up to 64 concurrent connections
- **JSON API**: RESTful communication with clients
- **Model management**: Intelligent model switching based on task type
- **Learning system**: Feedback-based improvement over time

### **AI Backend Integration**
- **Ollama support**: Local AI model integration
- **Multi-language**: English and Spanish support with automatic detection
- **Context-aware prompts**: System information, distribution, and user context
- **Model selection**: Automatic RAM-based model selection (smaller models for <6GB RAM)

## 📁 System Architecture

```
AI-OS/
├── kernel/                    # Linux kernel module
│   ├── ai_os.c              # Kernel-level integration
│   └── Makefile             # Kernel build system
├── userspace/
│   ├── daemon/              # AI daemon components
│   │   ├── ai_daemon.c     # Main daemon process
│   │   ├── model_manager.c # Intelligent model management
│   │   ├── context_manager.c # System context gathering
│   │   ├── kernel_bridge.c # Kernel communication
│   │   └── learning_system.c # Feedback-based learning
│   ├── client/              # Client applications
│   │   ├── ai_client.c     # Core client library
│   │   ├── ai-client.c     # CLI client application
│   │   └── ollama_client.c # Ollama AI backend
│   ├── shell-integration/   # Shell integration scripts
│   │   ├── ai-shell.sh     # Two-stage classification
│   │   ├── direct-ai-shell.sh # Direct interpretation
│   │   └── ultimate-ai-shell.sh # ML-powered shell
│   └── ai_os_common.h      # Shared definitions
├── scripts/
│   ├── install-ai-os.sh    # Complete installer
│   └── uninstall-ai-os.sh  # Clean uninstaller
└── Makefile                # Build system
```

## 🛠️ Installation

**One-command installation:**

```bash
# Clone and install
git clone https://github.com/saquib34/ai-os.git
cd ai-os
./scripts/install-ai-os.sh
```

The installer automatically:
- Detects your Linux distribution (Arch, Ubuntu, Debian)
- Installs all dependencies (gcc, make, curl, json-c, etc.)
- Installs and configures Ollama with optimal model selection
- Builds and installs kernel module, daemon, and clients
- Sets up systemd service, logging, and shell integration
- Configures permissions and starts the daemon

## 🎯 Usage Examples

### **Basic Natural Language Commands**
```bash
# File operations
list files in current directory
show me all files with details
create a new folder called my-project
delete all temporary files older than a week

# System monitoring
show running processes using lots of memory
check disk space on all mounted drives
monitor cpu usage in real time

# Package management
install python package for data science
update all system packages safely

# Git operations
git push and add all my changes
download latest code from the repository

# Network operations
check if the website is responding
scan for open network ports
```

### **Conversational Chat**
```bash
hello, how are you?
what's the weather like?
tell me a joke
who are you?
```

### **Shell Integration Controls**
```bash
ai-enable          # Enable natural language interpretation
ai-disable         # Disable interpretation
ai-auto-on         # Enable auto-execution (dangerous!)
ai-auto-off        # Require confirmation (safe)
ai-status          # Show system status
ai-help            # Show help
```

## 🔧 Advanced Features

### **Two-Stage Classification System with Jaccard Index**

AI-OS uses a sophisticated two-stage classification system to determine whether input should be treated as a command or chat:

#### **Stage 1: Jaccard Index Pattern Matching**
```bash
# Jaccard Index Formula: |A ∩ B| / |A ∪ B|
# Where A and B are sets of words from input and patterns
```

**How it works:**
1. **Tokenization**: Input is split into unique words
2. **Similarity Scoring**: Jaccard index calculated against saved patterns
3. **Threshold Decision**: 
   - Score ≥ 75% → Clear classification
   - Score < 75% → Ambiguous, proceed to Stage 2

**Example:**
```bash
Input: "list files in directory"
Pattern: "list files"
Jaccard: 2/3 = 66.7% → Ambiguous → Stage 2
```

#### **Stage 2: AI Classification Fallback**
When Jaccard index is ambiguous, AI-OS calls:
```bash
ai-client classify "your input here"
```

**Output:**
- `command` → Treat as shell command
- `chat` → Treat as conversational input

#### **Pattern Learning & Storage**
- **Command patterns**: `~/.ai-os-command-patterns.txt`
- **Chat patterns**: `~/.ai-os-chat-patterns.txt`
- **Automatic saving**: When AI fallback is used, input is saved to appropriate pattern file

#### **Debug Output**
Enable debug mode to see Jaccard scores:
```bash
# Shows similarity scores for troubleshooting
ai "list files"  # Will show Jaccard scores in debug mode
```

### **Pattern Learning System**
- **Automatic learning**: New patterns saved when AI fallback is used
- **Persistent storage**: Patterns saved to `~/.ai-os-command-patterns.txt` and `~/.ai-os-chat-patterns.txt`
- **Jaccard similarity**: Fast pattern matching with configurable thresholds
- **Debug output**: Shows similarity scores for troubleshooting

### **Context Awareness**
- **Current directory**: Considers working directory for file operations
- **Git status**: Includes branch and status information
- **System state**: Running processes, disk usage, network connections
- **User environment**: Environment variables and user context

### **Safety Features**
- **Multiple confirmation modes**: Y/n/e/h options for command execution
- **Dangerous command blocking**: Prevents unsafe operations
- **Command preview**: Shows interpreted command before execution
- **Edit capability**: Modify commands before execution

### **Performance Optimization**
- **Model selection**: Automatic RAM-based model selection
- **Caching**: Pattern matching for fast responses
- **Timeout handling**: Fallback patterns for AI timeouts
- **Log rotation**: Automatic log file management

## 🎛️ Configuration

### **System Configuration**
- **Config file**: `/etc/ai-os/config.json`
- **Log files**: `/var/log/ai-os/`
- **Pattern files**: `~/.ai-os-command-patterns.txt`, `~/.ai-os-chat-patterns.txt`
- **Feedback system**: `/etc/ai-os/feedback.json`

### **Shell Integration**
- **Bash**: Automatic integration via `~/.bashrc`
- **Zsh**: Support for zsh command not found handler
- **Fish**: Compatible with fish shell
- **Tab completion**: Smart suggestions for natural language

## 🧪 Testing

```bash
# Test basic functionality
ai "list files"

# Test classification
ai-client classify "hello world"

# Test daemon status
ai-client status

# Test interpretation
ai-client interpret "show running processes"
```

## 🗑️ Uninstallation

```bash
# Complete uninstall
./scripts/uninstall-ai-os.sh

# Or manual removal
sudo systemctl stop ai-os
sudo systemctl disable ai-os
sudo rm -rf /usr/local/sbin/ai-os-daemon /usr/local/bin/ai-client
sudo rm -rf /etc/ai-os /var/log/ai-os
sudo rm -f /lib/modules/$(uname -r)/extra/ai_os.ko
```

## 🔍 Troubleshooting

### **Common Issues**
1. **Daemon not running**: `sudo systemctl start ai-os`
2. **Permission errors**: Check log file permissions in `/var/log/ai-os/`
3. **Shell integration not working**: Reload shell with `source ~/.bashrc`
4. **AI timeout**: System uses fallback patterns for reliability

### **Debug Mode**
```bash
# Enable debug logging
sudo systemctl stop ai-os
sudo ai-os-daemon --debug
```

## 📊 System Requirements

- **OS**: Linux (Arch, Ubuntu, Debian supported)
- **RAM**: 4GB minimum, 8GB+ recommended
- **Storage**: 2GB free space
- **Dependencies**: gcc, make, curl, json-c, libcurl-dev

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## 📄 License

MIT License - see LICENSE file for details.

---

**AI-OS transforms your Linux shell into an intelligent, context-aware assistant that learns from your usage patterns and provides seamless natural language command interpretation.**

