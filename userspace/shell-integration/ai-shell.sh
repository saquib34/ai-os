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

# Function to save command patterns
save_command_pattern() {
    local pattern="$1"
    local pattern_file="$HOME/.ai-os-command-patterns.txt"
    # Create directory if it doesn't exist
    mkdir -p "$(dirname "$pattern_file")"
    # Add pattern if not already present
    if [ ! -f "$pattern_file" ] || ! grep -Fxq "$pattern" "$pattern_file"; then
        echo "$pattern" >> "$pattern_file"
        log_msg "Saved command pattern: $pattern"
    fi
}

# Function to save chat patterns
save_chat_pattern() {
    local pattern="$1"
    local pattern_file="$HOME/.ai-os-chat-patterns.txt"
    # Create directory if it doesn't exist
    mkdir -p "$(dirname "$pattern_file")"
    # Add pattern if not already present
    if [ ! -f "$pattern_file" ] || ! grep -Fxq "$pattern" "$pattern_file"; then
        echo "$pattern" >> "$pattern_file"
        log_msg "Saved chat pattern: $pattern"
    fi
}

# Function to load patterns from files
load_patterns_from_files() {
    local cmd_file="$HOME/.ai-os-command-patterns.txt"
    local chat_file="$HOME/.ai-os-chat-patterns.txt"
    
    # Load command patterns
    if [ -f "$cmd_file" ]; then
        while IFS= read -r pattern; do
            if [ -n "$pattern" ]; then
                command_patterns+=("$pattern")
            fi
        done < "$cmd_file"
    fi
    
    # Load chat patterns
    if [ -f "$chat_file" ]; then
        while IFS= read -r pattern; do
            if [ -n "$pattern" ]; then
                chat_patterns+=("$pattern")
            fi
        done < "$chat_file"
    fi
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

# Function to compute Jaccard index between two strings
jaccard_index() {
    local str1="$1"
    local str2="$2"
    # Tokenize and sort unique words
    local set1 set2 union intersection
    set1=$(echo "$str1" | tr ' ' '\n' | sort -u)
    set2=$(echo "$str2" | tr ' ' '\n' | sort -u)
    union=$(echo -e "$set1\n$set2" | sort -u | wc -l)
    intersection=$(echo -e "$set1\n$set2" | sort | uniq -d | wc -l)
    if [ "$union" -eq 0 ]; then
        echo 0
    else
        echo $(( 100 * intersection / union ))
    fi
}

# Function to classify input as command or chat using Jaccard index
classify_input() {
    local input="$1"
    local input_lc=$(echo "$input" | tr '[:upper:]' '[:lower:]')
    local max_cmd=0
    local max_chat=0
    local pattern
    for pattern in "${command_patterns[@]}"; do
        local score=$(jaccard_index "$input_lc" "$pattern")
        if [ "$score" -gt "$max_cmd" ]; then
            max_cmd=$score
        fi
    done
    for pattern in "${chat_patterns[@]}"; do
        local score=$(jaccard_index "$input_lc" "$pattern")
        if [ "$score" -gt "$max_chat" ]; then
            max_chat=$score
        fi
    done
    # Only return 'command' if it's a strong match, otherwise always use AI fallback
    if [ "$max_cmd" -ge 75 ] && [ "$max_cmd" -gt "$max_chat" ]; then
        echo "command"
    else
        echo "ambiguous"  # Always trigger AI fallback unless command is confident
    fi
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
    # Classification step
    local classification=$(classify_input "$natural_command")
    echo "DEBUG: Classification result: $classification"
    echo "DEBUG: AI_OS_AUTO_EXECUTE = $AI_OS_AUTO_EXECUTE"
    if [ "$classification" = "command" ]; then
        interpreted_command=$(ai-client interpret "$natural_command" 2>/dev/null)
        local exit_code=$?
        echo "DEBUG: ai-client interpret exit code: $exit_code"
        echo "DEBUG: interpreted_command = '$interpreted_command'"
        case $exit_code in
            0)
                echo "AI-OS: '$natural_command' → '$interpreted_command'"
                log_msg "Interpreted: $natural_command -> $interpreted_command"
                if [ "$AI_OS_AUTO_EXECUTE" = "1" ]; then
                    echo "AI-OS: Executing automatically..."
                    echo "DEBUG: About to execute: $interpreted_command"
                    log_msg "Auto-executing: $interpreted_command"
                    eval "$interpreted_command"
                    echo "DEBUG: Execution completed"
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
    elif [ "$classification" = "chat" ]; then
        # Use ai-client chat or fallback to echo
        local chat_response=$(ai-client chat "$natural_command" 2>/dev/null)
        if [ -z "$chat_response" ]; then
            chat_response="[AI-OS] $natural_command"
        fi
        echo "$chat_response"
        log_msg "Chat: $natural_command -> $chat_response"
    else
        # Stage 2: AI-based classification
        local ai_class=$(ai-client classify "$natural_command" 2>/dev/null)
        echo "DEBUG: AI classification result: $ai_class"
        if [ "$ai_class" = "command" ]; then
            # Save as command pattern
            save_command_pattern "$natural_command"
            # Interpret and execute the command directly
            interpreted_command=$(ai-client interpret "$natural_command" 2>/dev/null)
            local exit_code=$?
            echo "DEBUG: ai-client interpret exit code: $exit_code"
            echo "DEBUG: interpreted_command = '$interpreted_command'"
            case $exit_code in
                0)
                    echo "AI-OS: '$natural_command' → '$interpreted_command'"
                    log_msg "Interpreted: $natural_command -> $interpreted_command"
                    if [ "$AI_OS_AUTO_EXECUTE" = "1" ]; then
                        echo "AI-OS: Executing automatically..."
                        echo "DEBUG: About to execute: $interpreted_command"
                        log_msg "Auto-executing: $interpreted_command"
                        eval "$interpreted_command"
                        echo "DEBUG: Execution completed"
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
        elif [ "$ai_class" = "chat" ]; then
            # Save as chat pattern
            save_chat_pattern "$natural_command"
            local chat_response=$(ai-client chat "$natural_command" 2>/dev/null)
            if [ -z "$chat_response" ]; then
                chat_response="[AI-OS] $natural_command"
            fi
            echo "$chat_response"
            log_msg "Chat (AI classified): $natural_command -> $chat_response"
        elif [ -n "$ai_class" ]; then
            # If it's not 'command' or 'chat', treat as chat response
            save_chat_pattern "$natural_command"
            echo "$ai_class"
            log_msg "Chat (AI classified direct): $natural_command -> $ai_class"
        else
            echo "AI-OS: Could not classify input. Please rephrase."
            log_msg "Could not classify: $natural_command"
        fi
    fi
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

# Default command and chat patterns
command_patterns=(
    "list files" "show files" "find files" "ls" "show processes" "ps" "kill process" "delete file" "remove file" "cp" "mv" "cat" "grep" "df" "free" "top" "htop" "ping" "chmod" "chown" "mkdir" "rmdir" "touch" "nano" "vim" "systemctl" "journalctl" "apt install" "pacman -S" "git clone" "wget" "curl"
    "show me all files in this directory" "create a new folder called" "delete all temporary files" "copy important documents to backup" "show running processes" "kill all chrome processes" "restart the web server" "check what's using port" "check disk space" "show system memory usage" "monitor cpu usage" "find large files" "git push and add all changes" "install python package" "build the project" "run tests" "download the latest version" "scan for open ports" "backup database" "ls -la" "git status" "cd /home" "sudo apt update" "list files with details" "git status and current branch" "install python package requests" "create directory called" "download file from" "compress all log files" "update all system packages" "monitor cpu usage in real time" "find large files taking up disk space" "check if the website is responding" "scan for open network ports" "backup database to external server" "restart the apache web server service" "show me running processes using lots of memory" "delete all files in downloads" "move all images to pictures" "change permissions of all scripts" "show open network connections" "list all users" "add new user" "remove user" "change user password" "show system uptime" "show kernel version" "list installed packages" "update package list" "upgrade all packages" "remove unused packages" "show listening ports" "show firewall status" "enable firewall" "disable firewall" "show crontab" "edit crontab" "show environment variables" "set environment variable" "unset environment variable" "show current directory" "print working directory" "show hidden files" "show file contents" "edit file" "search for text in files" "replace text in files" "count lines in file" "sort lines in file" "show disk usage" "show memory usage" "show cpu info" "show hardware info" "show network interfaces" "restart network service" "show running services" "start service" "stop service" "restart service" "enable service" "disable service" "show service status" "show log files" "tail log file" "head log file" "clear log file" "archive log files" "extract archive" "compress folder" "uncompress file" "mount drive" "unmount drive" "format usb drive" "eject usb drive" "show battery status" "show power usage" "shutdown system" "reboot system" "suspend system" "hibernate system" "lock screen" "unlock screen" "change screen brightness" "change volume" "mute sound" "unmute sound" "take screenshot" "record screen" "open browser" "open terminal" "open text editor" "open file manager" "open settings" "open calculator" "open calendar" "open mail client" "open music player" "open video player" "open camera" "open contacts" "open notes" "open tasks" "open weather" "open maps" "open clock" "open news" "open social media" "open messaging app" "open phone" "open gallery" "open downloads" "open documents" "open pictures" "open videos" "open music" "open games" "open store" "open help" "open support" "open feedback" "open about" "open license" "open privacy policy" "open terms of service"
)
chat_patterns=(
    "hello" "hi" "hey" "how are you" "good morning" "good afternoon" "good evening" "good night" "what's up" "how's it going" "how do you do" "nice to meet you" "pleased to meet you" "thank you" "thanks" "thanks a lot" "thank you very much" "much appreciated" "you're welcome" "no problem" "no worries" "my pleasure" "anytime" "bye" "goodbye" "see you" "see you later" "take care" "have a nice day" "have a good day" "have a great day" "see you soon" "talk to you later" "catch you later" "farewell" "who are you" "what can you do" "help me" "can you help me" "tell me a joke" "tell me something interesting" "what is your name" "how old are you" "where are you from" "what do you do" "what's your purpose" "what languages do you speak" "do you have feelings" "do you have a family" "do you have friends" "do you like music" "do you like movies" "do you like games" "do you like sports" "do you like reading" "do you like traveling" "do you like food" "what's your favorite color" "what's your favorite food" "what's your favorite movie" "what's your favorite song" "what's your favorite book" "what's your favorite game" "what's your favorite sport" "what's your favorite animal" "what's your favorite place" "what's your favorite hobby" "what's your favorite subject" "what's your favorite season" "what's your favorite holiday" "what's your favorite quote" "what's your favorite memory" "what's your favorite thing" "what's your favorite person" "what's your favorite question" "what's your favorite answer" "what's your favorite joke" "what's your favorite riddle" "what's your favorite puzzle" "what's your favorite fact" "what's your favorite story" "what's your favorite experience" "what's your favorite dream" "what's your favorite wish" "what's your favorite goal" "what's your favorite achievement" "what's your favorite challenge" "what's your favorite adventure" "what's your favorite journey" "what's your favorite destination" "what's your favorite trip" "what's your favorite vacation" "what's your favorite travel" "what's your favorite place to visit" "what's your favorite country" "what's your favorite city" "what's your favorite language" "what's your favorite culture" "what's your favorite tradition" "what's your favorite festival" "what's your favorite celebration" "what's your favorite event" "what's your favorite activity" "what's your favorite pastime" "what's your favorite way to relax" "what's your favorite way to have fun" "what's your favorite way to learn" "what's your favorite way to work" "what's your favorite way to play" "what's your favorite way to create" "what's your favorite way to explore" "what's your favorite way to discover" "what's your favorite way to experience" "what's your favorite way to enjoy" "what's your favorite way to share" "what's your favorite way to connect" "what's your favorite way to communicate" "what's your favorite way to express" "what's your favorite way to think" "what's your favorite way to feel" "what's your favorite way to imagine" "what's your favorite way to dream" "what's your favorite way to wish" "what's your favorite way to hope" "what's your favorite way to believe" "what's your favorite way to love" "what's your favorite way to care" "what's your favorite way to help" "what's your favorite way to support" "what's your favorite way to inspire" "what's your favorite way to motivate" "what's your favorite way to encourage" "what's your favorite way to teach" "what's your favorite way to learn" "what's your favorite way to grow" "what's your favorite way to improve" "what's your favorite way to succeed" "what's your favorite way to achieve" "what's your favorite way to win" "what's your favorite way to lose" "what's your favorite way to try" "what's your favorite way to start" "what's your favorite way to finish" "what's your favorite way to begin" "what's your favorite way to end"
)

# Load patterns from files
load_patterns_from_files

# Function to check if input is a valid shell command
is_shell_command() {
    local input_word
    input_word=$(echo "$1" | awk '{print $1}')
    if command -v "$input_word" >/dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}