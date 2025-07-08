#!/bin/bash

LOG_FILE="/var/log/ai-os/working-ai.log"
LOG_MAX_SIZE=$((1024 * 1024)) # 1MB

log_msg() {
    # Rotate log if needed
    if [ -f "$LOG_FILE" ] && [ $(stat -c%s "$LOG_FILE") -gt $LOG_MAX_SIZE ]; then
        mv "$LOG_FILE" "$LOG_FILE.old"
    fi
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG_FILE"
}

aio_interpret() {
    local cmd="$1"
    echo "🤖 AI interpreting: $cmd"
    log_msg "Interpreting: $cmd"
    # Create focused prompt
    local prompt="Convert this natural language to a Linux shell command. Reply with ONLY the command, no explanation:

Natural language: $cmd
Command:"
    echo "🧠 Thinking... (may take 10-15 seconds)"
    # Get AI interpretation with timeout
    local result=$(timeout 25s ollama run phi3:mini "$prompt" 2>/dev/null | head -1 | sed 's/^[[:space:]]*//' | sed 's/[[:space:]]*$//')
    local status=$?
    if [ $status -eq 124 ]; then
        echo "⏰ AI timeout. Using quick patterns..."
        log_msg "Timeout for: $cmd. Using fallback."
        case "$cmd" in
            *"show"*"file"*|*"list"*"file"*) result="ls -la" ;;
            *"show"*"process"*|*"list"*"process"*) result="ps aux" ;;
            *"disk"*"space"*) result="df -h" ;;
            *"memory"*) result="free -h" ;;
            *) result="# AI timeout - try: $cmd" ;;
        esac
    fi
    if [ -n "$result" ] && [ "$result" != "${result#\#}" ]; then
        log_msg "Fallback or error result for: $cmd -> $result"
        echo "$result"
        return 1
    elif [ -n "$result" ]; then
        log_msg "Interpreted: $cmd -> $result"
        echo "💡 Interpreted as: $result"
        echo -n "🚀 Execute? [Y/n] "
        read -r response
        [[ "$response" != [nN]* ]] && eval "$result"
    else
        log_msg "Could not interpret: $cmd"
        echo "❓ Could not interpret command"
        return 1
    fi
}

# Override command not found for natural language
command_not_found_handle() {
    local command="$1"
    shift
    local full_command="$command $*"
    local word_count=$(echo "$full_command" | wc -w)
    if [ $word_count -ge 2 ]; then
        aio_interpret "$full_command"
    else
        echo "bash: $command: command not found"
        echo "💡 Try natural language like: show running processes"
        log_msg "Command not found: $command"
    fi
}

echo "🚀 Working AI Shell loaded!"
echo "💬 Try these commands:"
echo "   show files"
echo "   show running processes" 
echo "   check disk space"
