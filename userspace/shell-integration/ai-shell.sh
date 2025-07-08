#!/bin/bash
# AI-OS Shell Integration
# File: userspace/shell-integration/ai-shell.sh

LOG_FILE="/var/log/ai-os/ai-shell.log"
LOG_MAX_SIZE=$((1024 * 1024)) # 1MB

log_msg() {
    # Rotate log if needed
    if [ -f "$LOG_FILE" ] && [ $(stat -c%s "$LOG_FILE") -gt $LOG_MAX_SIZE ]; then
        mv "$LOG_FILE" "$LOG_FILE.old"
    fi
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG_FILE"
}

AI_OS_ENABLED=1
AI_OS_AUTO_EXECUTE=0
AI_OS_CONFIRMATION=1

# Check if AI daemon is running
check_ai_daemon() {
    if [ ! -S /var/run/ai-os.sock ]; then
        echo "AI-OS: Daemon not running. Start with 'sudo systemctl start ai-os'"
        log_msg "Daemon not running when checked."
        return 1
    fi
    return 0
}

# AI command interpretation function
ai() {
    if [ $# -eq 0 ]; then
        echo "Usage: ai <natural language command>"
        echo "Example: ai \"git push and add all files\""
        log_msg "ai called with no arguments."
        return 1
    fi
    if ! check_ai_daemon; then
        log_msg "ai called but daemon not running."
        return 1
    fi
    local natural_command="$*"
    local interpreted_command
    log_msg "Interpreting: $natural_command"
    # Use ai-client to interpret command
    interpreted_command=$(ai-client interpret "$natural_command" 2>/dev/null)
    local exit_code=$?
    case $exit_code in
        0)
            echo "AI-OS: '$natural_command' → '$interpreted_command'"
            log_msg "Interpreted: $natural_command -> $interpreted_command"
            if [ "$AI_OS_AUTO_EXECUTE" = "1" ]; then
                echo "AI-OS: Executing automatically..."
                log_msg "Auto-executing: $interpreted_command"
                eval "$interpreted_command"
            else
                echo -n "AI-OS: Execute this command? [Y/n] "
                read -r response
                case $response in
                    [nN]|[nN][oO])
                        echo "AI-OS: Command cancelled"
                        log_msg "User cancelled execution for: $interpreted_command"
                        ;;
                    *)
                        log_msg "User confirmed execution for: $interpreted_command"
                        eval "$interpreted_command"
                        ;;
                esac
            fi
            ;;
        2)
            echo "AI-OS: Command marked as unsafe and blocked"
            log_msg "Unsafe command blocked: $natural_command"
            return 1
            ;;
        3)
            echo "AI-OS: Command unclear, please rephrase"
            log_msg "Unclear command: $natural_command"
            return 1
            ;;
        *)
            echo "AI-OS: Failed to interpret command"
            log_msg "Failed to interpret: $natural_command"
            return 1
            ;;
    esac
}

# Enhanced command_not_found_handle for bash
command_not_found_handle() {
    local command="$1"
    shift
    local args="$*"
    # Check if this looks like a natural language command
    if [[ "$command" =~ ^[a-zA-Z] ]] && [[ "$command $args" =~ [[:space:]] ]]; then
        echo "AI-OS: Command '$command' not found. Trying AI interpretation..."
        log_msg "Command not found: $command $args. Trying AI interpretation."
        ai "$command $args"
        return $?
    fi
    # Fall back to default behavior
    echo "bash: $command: command not found"
    log_msg "Command not found: $command (not interpreted)"
    return 127
}

# Zsh command_not_found_handler
command_not_found_handler() {
    local command="$1"
    shift
    local args="$*"
    # Check if this looks like a natural language command
    if [[ "$command" =~ ^[a-zA-Z] ]] && [[ "$command $args" =~ [[:space:]] ]]; then
        echo "AI-OS: Command '$command' not found. Trying AI interpretation..."
        log_msg "Command not found: $command $args. Trying AI interpretation."
        ai "$command $args"
        return $?
    fi
    # Fall back to default behavior
    echo "zsh: command not found: $command"
    log_msg "Command not found: $command (not interpreted)"
    return 127
}

# Precommand hook for zsh (executes before each command)
preexec() {
    # This function is called just before a command is executed
    # We can use this to intercept and modify commands
    local command="$1"
    # Skip if AI-OS is disabled
    if [ "$AI_OS_ENABLED" != "1" ]; then
        return
    fi
    # Check for natural language patterns
    if [[ "$command" =~ "and then|and also|after that|first.*then" ]]; then
        echo "AI-OS: Detected complex command, interpreting..."
        log_msg "Detected complex command: $command"
        # Get interpretation
        local interpreted=$(ai-client interpret "$command" 2>/dev/null)
        if [ $? -eq 0 ] && [ -n "$interpreted" ]; then
            echo "AI-OS: Interpreted as: $interpreted"
            log_msg "Complex command interpreted as: $interpreted"
            if [ "$AI_OS_CONFIRMATION" = "1" ]; then
                echo -n "AI-OS: Execute interpreted command? [Y/n] "
                read -r response
                case $response in
                    [nN]|[nN][oO])
                        echo "AI-OS: Using original command"
                        log_msg "User chose original command over interpreted."
                        ;;
                    *)
                        print -s "$interpreted"  # Add to history
                        log_msg "User confirmed execution for complex interpreted: $interpreted"
                        eval "$interpreted"
                        return 130  # Interrupt original command
                        ;;
                esac
            fi
        fi
    fi
}

# Bash preexec equivalent using DEBUG trap
if [ -n "$BASH_VERSION" ]; then
    ai_preexec() {
        [ "$BASH_COMMAND" = "$PROMPT_COMMAND" ] && return
        # Skip if AI-OS is disabled
        if [ "$AI_OS_ENABLED" != "1" ]; then
            return
        fi
        local command="$BASH_COMMAND"
        # Check for natural language patterns
        if [[ "$command" =~ "and then"|"and also"|"after that" ]]; then
            echo "AI-OS: Detected complex command, interpreting..."
            log_msg "Detected complex command: $command"
            # Get interpretation
            local interpreted=$(ai-client interpret "$command" 2>/dev/null)
            if [ $? -eq 0 ] && [ -n "$interpreted" ]; then
                echo "AI-OS: Interpreted as: $interpreted"
                log_msg "Complex command interpreted as: $interpreted"
                if [ "$AI_OS_CONFIRMATION" = "1" ]; then
                    echo -n "AI-OS: Execute interpreted command? [Y/n] "
                    read -r response
                    case $response in
                        [nN]|[nN][oO])
                            echo "AI-OS: Using original command"
                            log_msg "User chose original command over interpreted."
                            ;;
                        *)
                            echo "AI-OS: Please run: $interpreted"
                            log_msg "User confirmed execution for complex interpreted: $interpreted (manual run)"
                            return 1
                            ;;
                    esac
                fi
            fi
        fi
    }
    trap 'ai_preexec' DEBUG
fi

# AI-OS control functions
ai-enable() {
    AI_OS_ENABLED=1
    echo "AI-OS: Natural language command interpretation enabled"
    log_msg "AI-OS enabled"
}
ai-disable() {
    AI_OS_ENABLED=0
    echo "AI-OS: Natural language command interpretation disabled"
    log_msg "AI-OS disabled"
}
ai-auto-on() {
    AI_OS_AUTO_EXECUTE=1
    echo "AI-OS: Auto-execution enabled (dangerous!)"
    log_msg "Auto-execution enabled"
}
ai-auto-off() {
    AI_OS_AUTO_EXECUTE=0
    echo "AI-OS: Auto-execution disabled"
    log_msg "Auto-execution disabled"
}
ai-confirm-on() {
    AI_OS_CONFIRMATION=1
    echo "AI-OS: Confirmation prompts enabled"
    log_msg "Confirmation enabled"
}
ai-confirm-off() {
    AI_OS_CONFIRMATION=0
    echo "AI-OS: Confirmation prompts disabled"
    log_msg "Confirmation disabled"
}
ai-status() {
    echo "AI-OS Status:"
    echo "  Enabled: $AI_OS_ENABLED"
    echo "  Auto-execute: $AI_OS_AUTO_EXECUTE"
    echo "  Confirmation: $AI_OS_CONFIRMATION"
    log_msg "Status checked: Enabled=$AI_OS_ENABLED, Auto=$AI_OS_AUTO_EXECUTE, Confirm=$AI_OS_CONFIRMATION"
    if check_ai_daemon; then
        ai-client status 2>/dev/null | head -10
    fi
}
ai-help() {
    cat << EOF
AI-OS Shell Integration Commands:

  ai <command>          - Interpret and optionally execute natural language command
  ai-enable            - Enable AI command interpretation
  ai-disable           - Disable AI command interpretation  
  ai-auto-on           - Enable automatic execution (no confirmation)
  ai-auto-off          - Disable automatic execution
  ai-confirm-on        - Enable confirmation prompts
  ai-confirm-off       - Disable confirmation prompts
  ai-status            - Show AI-OS status
  ai-help              - Show this help

Examples:
  ai "git push and add all files"
  ai "install python package numpy"
  ai "show me running processes using lots of memory"
EOF
}

# Initialize
if check_ai_daemon > /dev/null 2>&1; then
    echo "AI-OS: Shell integration loaded. Type 'ai-help' for usage information."
else
    echo "AI-OS: Shell integration loaded, but daemon is not running."
    echo "AI-OS: Start daemon with 'sudo systemctl start ai-os'"
fi

# Tab completion for ai command
if [ -n "$BASH_VERSION" ]; then
    _ai_completion() {
        local cur="${COMP_WORDS[COMP_CWORD]}"
        local suggestions="git install create delete show list check download compile build start stop restart"
        COMPREPLY=($(compgen -W "$suggestions" -- "$cur"))
    }
    complete -F _ai_completion ai
elif [ -n "$ZSH_VERSION" ]; then
    _ai() {
        local suggestions="git install create delete show list check download compile build start stop restart"
        _arguments '*:command:($suggestions)'
    }
    compdef _ai ai
fi