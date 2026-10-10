#!/bin/bash
set -e

if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root (sudo ./uninstall.sh)"
    exit 1
fi

echo "[*] Uninstalling Xmod Memory Framework..."

# Unload driver if it's running
if lsmod | grep -q "^xmod "; then
    echo "[+] Unloading kernel module..."
    rmmod xmod || true
fi

# Remove files
rm -f /usr/local/bin/Xmod
rm -f /usr/local/bin/xmod-core
rm -f /lib/modules/$(uname -r)/extra/xmod.ko
rm -f /usr/share/applications/xmod.desktop

depmod -a

echo "[+] Uninstall complete."
