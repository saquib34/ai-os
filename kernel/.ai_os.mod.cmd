savedcmd_ai_os.mod := printf '%s\n'   ai_os.o | awk '!x[$$0]++ { print("./"$$0) }' > ai_os.mod
