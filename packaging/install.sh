#!/bin/bash
set -e

if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root (sudo ./install.sh)"
    exit 1
fi

echo "[*] Installing Xmod Memory Framework..."

# 1. Install Binaries
echo "[+] Installing binaries to /usr/local/bin/"
cp Xmod /usr/local/bin/
cp xmod-core /usr/local/bin/
chmod 755 /usr/local/bin/Xmod
chmod 755 /usr/local/bin/xmod-core

# 2. Install Kernel Module
echo "[+] Installing kernel module to /lib/modules/$(uname -r)/extra/"
mkdir -p /lib/modules/$(uname -r)/extra/
cp driver/xmod.ko /lib/modules/$(uname -r)/extra/
depmod -a

# 3. Install Desktop Entry
echo "[+] Installing desktop entry to /usr/share/applications/"
cp packaging/xmod.desktop /usr/share/applications/

echo "[+] Setting permissions for /dev/xmod (udev rule optional, defaulting to 0600 root)"

echo ""
echo "========================================="
echo " Installation Complete!"
echo "========================================="
echo " You can now find 'Xmod' in your application launcher."
echo " Or run it via terminal: sudo Xmod"
echo ""
echo " To load the driver manually: sudo modprobe xmod"
echo " To unload the driver:        sudo rmmod xmod"
