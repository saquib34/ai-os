#!/bin/bash
# Direct Natural Language Shell Integration
# File: userspace/shell-integration/direct-ai-shell.sh

LOG_FILE="/var/log/ai-os/direct-ai-shell.log"
LOG_MAX_SIZE=$((1024 * 1024)) # 1MB

log_msg() {
    # Rotate log if needed
    if [ -f "$LOG_FILE" ] && [ $(stat -c%s "$LOG_FILE") -gt $LOG_MAX_SIZE ]; then
        mv "$LOG_FILE" "$LOG_FILE.old"
    fi
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG_FILE"
}

AI_OS_DIRECT_MODE=1
AI_OS_THRESHOLD=3  # Minimum words to trigger AI interpretation

# Function to detect if a command looks like natural language
is_natural_language() {
    local cmd="$1"
    local word_count=$(echo "$cmd" | wc -w)
    # Must have at least 3 words
    if [ $word_count -lt $AI_OS_THRESHOLD ]; then
        return 1
    fi
    # Check for natural language patterns
    if [[ "$cmd" =~ (and|then|with|using|all|show|list|create|install|download|check) ]]; then
        return 0
    fi
    # Check for common natural phrases
    if [[ "$cmd" =~ "files in"|"running processes"|"disk space"|"memory usage"|"git status" ]]; then
        return 0
    fi
    return 1
}

# Enhanced command_not_found_handle for bash
command_not_found_handle() {
    local command="$1"
    shift
    local args="$*"
    local full_command="$command $args"
    # Check if this looks like natural language
    if is_natural_language "$full_command"; then
        echo "🤖 Interpreting: $full_command"
        log_msg "Interpreting: $full_command (command_not_found_handle)"
        # Get interpretation from AI
        local interpreted
        interpreted=$(ai-client interpret "$full_command" 2>/dev/null)
        local result=$?
        if [ $result -eq 0 ] && [ -n "$interpreted" ]; then
            echo "💡 Interpreted as: $interpreted"
            log_msg "Interpreted: $full_command -> $interpreted"
            # Auto-execute or ask for confirmation
            if [ "$AI_OS_AUTO_EXECUTE" = "1" ]; then
                echo "⚡ Executing..."
                log_msg "Auto-executing: $interpreted"
                eval "$interpreted"
            else
                echo -n "🚀 Execute this command? [Y/n] "
                read -r response
                case $response in
                    [nN]|[nN][oO])
                        echo "❌ Cancelled"
                        log_msg "User cancelled execution for: $interpreted"
                        ;;
                    *)
                        log_msg "User confirmed execution for: $interpreted"
                        eval "$interpreted"
                        ;;
                esac
            fi
            return $?
        else
            echo "❓ Could not interpret natural language command"
            echo "💭 Try: ai \"$full_command\""
            log_msg "Failed to interpret: $full_command"
        fi
    else
        # Fall back to standard error
        echo "bash: $command: command not found"
        echo "💡 Tip: Use natural language (3+ words) for AI interpretation"
        log_msg "Command not found: $command (not interpreted)"
    fi
    return 127
}

# Zsh equivalent
command_not_found_handler() {
    local command="$1"
    shift
    local args="$*"
    local full_command="$command $args"
    if is_natural_language "$full_command"; then
        echo "🤖 Interpreting: $full_command"
        log_msg "Interpreting: $full_command (command_not_found_handler)"
        local interpreted
        interpreted=$(ai-client interpret "$full_command" 2>/dev/null)
        local result=$?
        if [ $result -eq 0 ] && [ -n "$interpreted" ]; then
            echo "💡 Interpreted as: $interpreted"
            log_msg "Interpreted: $full_command -> $interpreted"
            if [ "$AI_OS_AUTO_EXECUTE" = "1" ]; then
                echo "⚡ Executing..."
                log_msg "Auto-executing: $interpreted"
                eval "$interpreted"
            else
                echo -n "🚀 Execute this command? [Y/n] "
                read -r response
                case $response in
                    [nN]|[nN][oO])
                        echo "❌ Cancelled"
                        log_msg "User cancelled execution for: $interpreted"
                        ;;
                    *)
                        log_msg "User confirmed execution for: $interpreted"
                        eval "$interpreted"
                        ;;
                esac
            fi
            return $?
        else
            echo "❓ Could not interpret natural language command"
            log_msg "Failed to interpret: $full_command"
        fi
    else
        echo "zsh: command not found: $command"
        echo "💡 Tip: Use natural language (3+ words) for AI interpretation"
        log_msg "Command not found: $command (not interpreted)"
    fi
    return 127
}

# Global preexec hook for bash (intercepts ALL commands)
if [ -n "$BASH_VERSION" ]; then
    ai_preexec_direct() {
        local cmd="$BASH_COMMAND"
        # Skip if it's a built-in or existing command
        if command -v "$(echo "$cmd" | awk '{print $1}')" >/dev/null 2>&1; then
            return 0
        fi
        # Skip if disabled
        if [ "$AI_OS_DIRECT_MODE" != "1" ]; then
            return 0
        fi
        # Check if this looks like natural language
        if is_natural_language "$cmd"; then
            echo "🤖 Intercepting: $cmd"
            log_msg "Intercepting: $cmd (bash preexec)"
            local interpreted
            interpreted=$(ai-client interpret "$cmd" 2>/dev/null)
            local result=$?
            if [ $result -eq 0 ] && [ -n "$interpreted" ]; then
                echo "💡 Interpreted as: $interpreted"
                log_msg "Interpreted: $cmd -> $interpreted"
                if [ "$AI_OS_AUTO_EXECUTE" = "1" ]; then
                    echo "⚡ Auto-executing..."
                    log_msg "Auto-executing: $interpreted"
                    history -s "$interpreted"
                    eval "$interpreted"
                    return 130  # Interrupt original command
                else
                    echo -n "🚀 Execute interpreted command instead? [Y/n] "
                    read -r response
                    case $response in
                        [nN]|[nN][oO])
                            echo "❌ Using original command"
                            log_msg "User chose original command over interpreted."
                            ;;
                        *)
                            log_msg "User confirmed execution for: $interpreted (bash preexec)"
                            history -s "$interpreted"
                            eval "$interpreted"
                            return 130  # Interrupt original command
                            ;;
                    esac
                fi
            fi
        fi
        return 0
    }
    # Enable the preexec hook
    trap 'ai_preexec_direct' DEBUG
fi

# Zsh preexec hook
if [ -n "$ZSH_VERSION" ]; then
    autoload -U add-zsh-hook
    ai_preexec_zsh() {
        local cmd="$1"
        # Skip if it's a built-in or existing command
        if command -v "$(echo "$cmd" | awk '{print $1}')" >/dev/null 2>&1; then
            return 0
        fi
        # Skip if disabled
        if [ "$AI_OS_DIRECT_MODE" != "1" ]; then
            return 0
        fi
        if is_natural_language "$cmd"; then
            echo "🤖 Intercepting: $cmd"
            log_msg "Intercepting: $cmd (zsh preexec)"
            local interpreted
            interpreted=$(ai-client interpret "$cmd" 2>/dev/null)
            local result=$?
            if [ $result -eq 0 ] && [ -n "$interpreted" ]; then
                echo "💡 Interpreted as: $interpreted"
                log_msg "Interpreted: $cmd -> $interpreted"
                if [ "$AI_OS_AUTO_EXECUTE" = "1" ]; then
                    echo "⚡ Auto-executing..."
                    log_msg "Auto-executing: $interpreted"
                    print -s "$interpreted"  # Add to history
                    eval "$interpreted"
                else
                    echo -n "🚀 Execute interpreted command instead? [Y/n] "
                    read -r response
                    case $response in
                        [nN]|[nN][oO])
                            echo "❌ Using original command"
                            log_msg "User chose original command over interpreted."
                            ;;
                        *)
                            log_msg "User confirmed execution for: $interpreted (zsh preexec)"
                            print -s "$interpreted"  # Add to history
                            eval "$interpreted"
                            ;;
                    esac
                fi
            fi
        fi
    }
    add-zsh-hook preexec ai_preexec_zsh
fi

# Control functions
direct-ai-enable() {
    AI_OS_DIRECT_MODE=1
    echo "🟢 Direct AI mode enabled"
    echo "💬 Now you can use natural language directly:"
    echo "   Example: show running processes"
    echo "   Example: git push and add all files"
    log_msg "Direct AI mode enabled"
}
direct-ai-disable() {
    AI_OS_DIRECT_MODE=0
    echo "🔴 Direct AI mode disabled"
    echo "💬 Use 'ai \"command\"' for interpretation"
    log_msg "Direct AI mode disabled"
}
direct-ai-auto-on() {
    AI_OS_AUTO_EXECUTE=1
    echo "⚡ Auto-execution enabled (be careful!)"
    log_msg "Auto-execution enabled"
}

direct-ai-auto-off() {
    AI_OS_AUTO_EXECUTE=0
    echo "🛡️ Auto-execution disabled (confirmation required)"
    log_msg "Auto-execution disabled"
}

direct-ai-status() {
    echo "Direct AI Mode Status:"
    echo "====================="
    echo "Direct Mode: $([ "$AI_OS_DIRECT_MODE" = "1" ] && echo "🟢 Enabled" || echo "🔴 Disabled")"
    echo "Auto Execute: $([ "$AI_OS_AUTO_EXECUTE" = "1" ] && echo "⚡ Enabled" || echo "🛡️ Disabled")"
    echo "Word Threshold: $AI_OS_THRESHOLD words minimum"
    echo ""
    echo "Try these natural commands:"
    echo "  show running processes"
    echo "  list files with details"
    echo "  git status and current branch"
    echo "  install python package requests"
    echo "  create directory called test"
}

direct-ai-help() {
    cat << 'EOF'
🤖 Direct AI Natural Language Shell Integration

OVERVIEW:
Type natural language commands directly in your shell - no "ai" prefix needed!

EXAMPLES:
  show running processes              → ps aux
  list files with details            → ls -la  
  git push and add all files         → git add . && git push
  install python package numpy       → pip install numpy
  create directory called my-project → mkdir -p "my-project"
  check disk space                   → df -h
  download file from example.com     → wget example.com

CONTROLS:
  direct-ai-enable     - Enable direct natural language mode
  direct-ai-disable    - Disable direct mode (use ai "command" instead)
  direct-ai-auto-on    - Auto-execute interpreted commands (dangerous!)
  direct-ai-auto-off   - Require confirmation before execution (safe)
  direct-ai-status     - Show current configuration
  direct-ai-help       - Show this help

REQUIREMENTS:
- Commands must be 3+ words to trigger AI interpretation
- Must contain natural language patterns (and, with, show, etc.)
- AI-OS daemon must be running

SAFETY:
- Dangerous commands are blocked by AI safety filters
- Confirmation prompts prevent accidental execution
- Original commands are preserved in shell history

EOF
}

# Initialize
if command -v ai-client >/dev/null 2>&1; then
    echo "🤖 Direct AI mode loaded!"
    echo "💬 Type 'direct-ai-help' for usage instructions"
    
    # Enable by default
    direct-ai-enable
    direct-ai-auto-off  # Safe mode by default
else
    echo "⚠️  AI-OS client not found. Install AI-OS first."
fi