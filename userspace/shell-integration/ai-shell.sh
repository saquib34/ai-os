#!/bin/bash
# AI-OS Shell Integration
# File: userspace/shell-integration/ai-shell.sh

# Source this file in your ~/.bashrc or ~/.zshrc to enable AI command interpretation

AI_OS_ENABLED=1
AI_OS_AUTO_EXECUTE=0
AI_OS_CONFIRMATION=1

# Check if AI daemon is running
check_ai_daemon() {
    if ! pgrep -f "ai-os-daemon" > /dev/null; then
        echo "AI-OS: Daemon not running. Start with 'sudo systemctl start ai-os'"
        return 1
    fi
    return 0
}

# AI command interpretation function
ai() {
    if [ $# -eq 0 ]; then
        echo "Usage: ai <natural language command>"
        echo "Example: ai \"git push and add all files\""
        return 1
    fi
    
    if ! check_ai_daemon; then
        return 1
    fi
    
    local natural_command="$*"
    local interpreted_command
    
    # Use ai-client to interpret command
    interpreted_command=$(ai-client interpret "$natural_command" 2>/dev/null)
    local exit_code=$?
    
    case $exit_code in
        0)
            echo "AI-OS: '$natural_command' → '$interpreted_command'"
            
            if [ "$AI_OS_AUTO_EXECUTE" = "1" ]; then
                echo "AI-OS: Executing automatically..."
                eval "$interpreted_command"
            else
                echo -n "AI-OS: Execute this command? [Y/n] "
                read -r response
                case $response in
                    [nN]|[nN][oO])
                        echo "AI-OS: Command cancelled"
                        ;;
                    *)
                        eval "$interpreted_command"
                        ;;
                esac
            fi
            ;;
        2)
            echo "AI-OS: Command marked as unsafe and blocked"
            return 1
            ;;
        3)
            echo "AI-OS: Command unclear, please rephrase"
            return 1
            ;;
        *)
            echo "AI-OS: Failed to interpret command"
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
        ai "$command $args"
        return $?
    fi
    
    # Fall back to default behavior
    echo "bash: $command: command not found"
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
        ai "$command $args"
        return $?
    fi
    
    # Fall back to default behavior
    echo "zsh: command not found: $command"
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
        
        # Get interpretation
        local interpreted=$(ai-client interpret "$command" 2>/dev/null)
        if [ $? -eq 0 ] && [ -n "$interpreted" ]; then
            echo "AI-OS: Interpreted as: $interpreted"
            
            if [ "$AI_OS_CONFIRMATION" = "1" ]; then
                echo -n "AI-OS: Execute interpreted command? [Y/n] "
                read -r response
                case $response in
                    [nN]|[nN][oO])
                        echo "AI-OS: Using original command"
                        ;;
                    *)
                        # Replace the command
                        print -s "$interpreted"  # Add to history
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
            
            # Get interpretation
            local interpreted=$(ai-client interpret "$command" 2>/dev/null)
            if [ $? -eq 0 ] && [ -n "$interpreted" ]; then
                echo "AI-OS: Interpreted as: $interpreted"
                
                if [ "$AI_OS_CONFIRMATION" = "1" ]; then
                    echo -n "AI-OS: Execute interpreted command? [Y/n] "
                    read -r response
                    case $response in
                        [nN]|[nN][oO])
                            echo "AI-OS: Using original command"
                            ;;
                        *)
                            # This is tricky in bash - we can't easily replace the command
                            echo "AI-OS: Please run: $interpreted"
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
}

ai-disable() {
    AI_OS_ENABLED=0
    echo "AI-OS: Natural language command interpretation disabled"
}

ai-auto-on() {
    AI_OS_AUTO_EXECUTE=1
    echo "AI-OS: Auto-execution enabled (dangerous!)"
}

ai-auto-off() {
    AI_OS_AUTO_EXECUTE=0
    echo "AI-OS: Auto-execution disabled"
}

ai-confirm-on() {
    AI_OS_CONFIRMATION=1
    echo "AI-OS: Confirmation prompts enabled"
}

ai-confirm-off() {
    AI_OS_CONFIRMATION=0
    echo "AI-OS: Confirmation prompts disabled"
}

ai-status() {
    echo "AI-OS Status:"
    echo "  Enabled: $AI_OS_ENABLED"
    echo "  Auto-execute: $AI_OS_AUTO_EXECUTE"
    echo "  Confirmation: $AI_OS_CONFIRMATION"
    
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
  ai "create directory called projects and go into it"

Environment Variables:
  AI_OS_ENABLED        - Enable/disable AI interpretation (1/0)
  AI_OS_AUTO_EXECUTE   - Auto-execute interpreted commands (1/0)
  AI_OS_CONFIRMATION   - Show confirmation prompts (1/0)

Note: AI-OS daemon must be running for interpretation to work.
Start with: sudo systemctl start ai-os
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

# --- AI-OS auto-interpret shell wrapper ---
aios_intercept() {
    # Don't intercept empty commands, or commands starting with a space (for raw shell)
    [[ -z "$BASH_COMMAND" || "$BASH_COMMAND" =~ ^[[:space:]] ]] && return

    # Don't intercept builtins, functions, or already-interpreted commands
    type "$BASH_COMMAND" &>/dev/null && return

    # Try to interpret with AI-OS
    interpreted=$(ai-client interpret "$BASH_COMMAND" 2>/dev/null)
    if [[ $? -eq 0 && -n "$interpreted" ]]; then
        echo "AI-OS: '$BASH_COMMAND' → $interpreted"
        eval "$interpreted"
        # Prevent the original command from running
        READLINE_LINE=""
        return 1
    fi
}
trap aios_intercept DEBUG
# --- End AI-OS auto-interpret shell wrapper ---