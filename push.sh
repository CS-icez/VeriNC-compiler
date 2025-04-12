#!/bin/bash

LOCAL_DIR="$(pwd)"
REMOTE_USER="ubuntu"
REMOTE_HOST="10.129.243.67"
REMOTE_BASE_DIR="verinc"

# Function to send a directory.
send_directory() {
  local dir_name=$1
  if [[ -d "$LOCAL_DIR/$dir_name" ]]; then
    ssh "$REMOTE_USER@$REMOTE_HOST" "mkdir -p \"$REMOTE_BASE_DIR/$dir_name\""
    scp -r "$LOCAL_DIR/$dir_name" "$REMOTE_USER@$REMOTE_HOST:\"$REMOTE_BASE_DIR\""
  fi
}

# Function to send a file.
send_file() {
  local file_name=$1
  if [[ -e "$LOCAL_DIR/$file_name" ]]; then
    ssh "$REMOTE_USER@$REMOTE_HOST" "mkdir -p \"$REMOTE_BASE_DIR\""
    scp "$LOCAL_DIR/$file_name" "$REMOTE_USER@$REMOTE_HOST:\"$REMOTE_BASE_DIR/$file_name\""
  fi
}

send_directory "protocols"
send_directory "src"
send_directory "include"
send_directory "lib"

# send_file "verinc"
send_file "Makefile"
