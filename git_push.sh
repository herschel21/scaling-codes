#!/bin/bash

# Generate a random commit message
messages=("Update files" "Fix bug" "Refactor code" "Add new feature" "Improve performance" "Minor changes" "Code cleanup" "Sync changes" "Enhance documentation" "Optimize logic")
random_message=${messages[$RANDOM % ${#messages[@]}]}

# Git commands
git add .
git commit -m "$random_message"
git push

echo "Changes pushed with commit message: '$random_message'"

