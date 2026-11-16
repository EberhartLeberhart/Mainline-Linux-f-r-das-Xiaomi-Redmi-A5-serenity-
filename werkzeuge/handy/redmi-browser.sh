#!/bin/bash
# Redmi A5: Webbrowser als .deb installieren (kein Snap) + pavucontrol, dann Tondienst der Desktop-Sitzung zeigen
set -e
df -h / | tail -1
apt-get install -y wget gpg >/dev/null
install -d -m 0755 /etc/apt/keyrings
wget -q https://packages.mozilla.org/apt/repo-signing-key.gpg -O /etc/apt/keyrings/packages.mozilla.org.asc
echo "deb [signed-by=/etc/apt/keyrings/packages.mozilla.org.asc] https://packages.mozilla.org/apt mozilla main" > /etc/apt/sources.list.d/mozilla.list
printf 'Package: *\nPin: origin packages.mozilla.org\nPin-Priority: 1000\n\nPackage: firefox*\nPin: release o=Ubuntu\nPin-Priority: -1\n' > /etc/apt/preferences.d/mozilla
apt-get update -qq || true
if apt-cache policy firefox | grep -q "packages.mozilla.org"; then
  apt-get install -y firefox firefox-l10n-de pavucontrol && echo "=== FIREFOX INSTALLIERT ==="
else
  echo "Kein arm64-Firefox bei Mozilla -> GNOME Web"
  rm -f /etc/apt/sources.list.d/mozilla.list /etc/apt/preferences.d/mozilla
  apt-get update -qq || true
  apt-get install -y epiphany-browser pavucontrol && echo "=== GNOME WEB INSTALLIERT ==="
fi
U=$(id -u tunnel-handy)
echo "--- Tondienst in der Desktop-Sitzung:"
sudo -u tunnel-handy XDG_RUNTIME_DIR=/run/user/$U wpctl status 2>&1 | head -30 || true
