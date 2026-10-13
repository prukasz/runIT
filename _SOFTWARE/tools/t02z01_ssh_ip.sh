#!/usr/bin/env bash

HOST="pbl5zima2026esp32idf"
USER_VM="student"
KEY="$HOME/.ssh/id_ed25519_pbl5"
SSH="ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=5 -i $KEY $USER_VM@$HOST"

mkdir -p "$HOME/.ssh"
[[ -f "$KEY" ]] || ssh-keygen -t ed25519 -N "" -f "$KEY" -q

if ! ssh -o BatchMode=yes -i "$KEY" -o StrictHostKeyChecking=accept-new "$USER_VM@$HOST" true 2>/dev/null; then
    cat "$KEY.pub" | ssh -o StrictHostKeyChecking=accept-new "$USER_VM@$HOST" \
        'mkdir -p ~/.ssh; read -r k; grep -qxF "$k" ~/.ssh/authorized_keys 2>/dev/null || echo "$k" >> ~/.ssh/authorized_keys'
fi

$SSH 'ip -4 -o addr show' | awk '$2 ~ /^(en|eth)/ { sub(/\/.*/, "", $4); print $2 ": " $4 }'
