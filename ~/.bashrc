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