#!/bin/bash

ai_fast() {
    local cmd="$1"
    echo "🤖 Processing: $cmd"
    
    # INSTANT pattern matching for common commands
    case "$cmd" in
        *"show files"*|*"list files"*|*"show file"*)
            echo "💡 Instant match: ls -la"
            ls -la
            return 0
            ;;
        *"show"*"process"*|*"running"*"process"*|*"list"*"process"*)
            echo "💡 Instant match: ps aux"
            ps aux
            return 0
            ;;
        *"disk"*"space"*|*"check"*"disk"*)
            echo "💡 Instant match: df -h"
            df -h
            return 0
            ;;
        *"memory"*"usage"*|*"show"*"memory"*)
            echo "💡 Instant match: free -h"
            free -h
            return 0
            ;;
        *"git"*"status"*)
            echo "💡 Instant match: git status"
            git status
            return 0
            ;;
        *"git"*"push"*"add"*)
            echo "💡 Instant match: git add . && git push"
            echo -n "🚀 Execute? [Y/n] "
            read -r response
            [[ "$response" != [nN]* ]] && git add . && git push
            return 0
            ;;
        *"current"*"directory"*|*"where"*"am"*)
            echo "💡 Instant match: pwd"
            pwd
            return 0
            ;;
        *"go"*"home"*|*"home"*"directory"*)
            echo "💡 Instant match: cd ~"
            cd ~
            pwd
            return 0
            ;;
        *"install"*"python"*"package"*)
            local pkg=$(echo "$cmd" | grep -o 'package [^[:space:]]*' | cut -d' ' -f2)
            if [ -n "$pkg" ]; then
                echo "💡 Instant match: pip install $pkg"
                echo -n "🚀 Install $pkg? [Y/n] "
                read -r response
                [[ "$response" != [nN]* ]] && pip install "$pkg"
            else
                echo "💡 Usage: install python package <name>"
            fi
            return 0
            ;;
        *"create"*"directory"*|*"make"*"directory"*|*"create"*"folder"*)
            local dir=$(echo "$cmd" | sed -n 's/.*called[[:space:]]\+\([^[:space:]]\+\).*/\1/p')
            if [ -n "$dir" ]; then
                echo "💡 Instant match: mkdir -p $dir"
                mkdir -p "$dir"
                echo "✅ Created directory: $dir"
            else
                echo "💡 Usage: create directory called <name>"
            fi
            return 0
            ;;
    esac
    
    # If no instant match, offer to try AI (but don't auto-run it)
    echo "❓ No instant match found."
    echo "🧠 Try AI interpretation? (takes 15+ seconds) [y/N]"
    read -r use_ai
    
    if [[ "$use_ai" =~ ^[Yy] ]]; then
        echo "🧠 Using AI... please wait..."
        # Only call Ollama if user explicitly wants it
        local result=$(timeout 30s ollama run phi3:mini "Convert to Linux command: $cmd" 2>/dev/null | head -1)
        if [ -n "$result" ]; then
            echo "💡 AI says: $result"
            echo -n "🚀 Execute? [Y/n] "
            read -r response
            [[ "$response" != [nN]* ]] && eval "$result"
        else
            echo "❓ AI didn't respond"
        fi
    else
        echo "💡 Try more specific commands like:"
        echo "   show files, show processes, check disk space"
    fi
}

# Override command not found
command_not_found_handle() {
    local command="$1"
    shift
    local full_command="$command $*"
    
    local word_count=$(echo "$full_command" | wc -w)
    if [ $word_count -ge 2 ]; then
        ai_fast "$full_command"
    else
        echo "bash: $command: command not found"
        echo "💡 Try: show files, show processes, check disk space"
    fi
}

echo "⚡ Fast AI Shell loaded!"
echo "🚀 Instant commands available:"
echo "   show files"
echo "   show running processes"
echo "   check disk space"
echo "   show memory usage"
echo "   git status"
echo "   current directory"
echo "   create directory called <name>"
echo "   install python package <name>"
