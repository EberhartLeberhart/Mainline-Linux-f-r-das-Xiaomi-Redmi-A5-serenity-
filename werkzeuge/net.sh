#!/bin/bash
# USB-Netz zum Redmi einrichten (nach jedem Handy-Neustart)
echo ">>> warte auf Redmi-USB-Netz ..."
for i in $(seq 1 60); do
    IF=$(ip -br link | awk '/^enx/{print $1; exit}')
    [ -n "$IF" ] && break
    sleep 1
done
[ -z "$IF" ] && { echo "kein Redmi gefunden"; exit 1; }
sudo nmcli dev set $IF managed no 2>/dev/null
sudo ip addr add 192.168.7.1/24 dev $IF 2>/dev/null
sudo ip link set $IF up
sudo sysctl -qw net.ipv4.ip_forward=1
sudo iptables -t nat -C POSTROUTING -o wlp1s0 -j MASQUERADE 2>/dev/null || sudo iptables -t nat -A POSTROUTING -o wlp1s0 -j MASQUERADE
sudo iptables -C FORWARD -i $IF -j ACCEPT 2>/dev/null || sudo iptables -A FORWARD -i $IF -j ACCEPT
sudo iptables -C FORWARD -o $IF -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || sudo iptables -A FORWARD -o $IF -m state --state RELATED,ESTABLISHED -j ACCEPT
ping -c 1 -W 3 192.168.7.2 >/dev/null && echo ">>> Redmi erreichbar ueber $IF" || echo ">>> $IF da, aber Redmi antwortet (noch) nicht"
