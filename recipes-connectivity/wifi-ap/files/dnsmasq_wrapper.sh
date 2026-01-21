#!/bin/sh
#
# dnsmasq_wrapper.sh - Dynamic dnsmasq launcher for WiFi AP
# Reads IP address from /etc/wifi/ap_config and calculates DHCP range
#

AP_CONFIG_FILE=/etc/wifi/ap_config
DEFAULT_IP="192.168.172.1"
INTERFACE="${1:-wlan1}"

# Load config file if it exists
if [ -f "$AP_CONFIG_FILE" ]; then
    . "$AP_CONFIG_FILE"
fi

# Use configured IP or default
IP_ADDRESS="${IP_ADDRESS:-$DEFAULT_IP}"

# Extract subnet prefix (first 3 octets)
# e.g., 192.168.172.1 -> 192.168.172
SUBNET_PREFIX=$(echo "$IP_ADDRESS" | sed 's/\.[0-9]*$//')

# Calculate DHCP range based on subnet
DHCP_RANGE_START="${SUBNET_PREFIX}.50"
DHCP_RANGE_END="${SUBNET_PREFIX}.200"
LEASE_TIME="12h"

# Gateway is the AP IP address
GATEWAY="$IP_ADDRESS"

echo "Starting dnsmasq with dynamic configuration:"
echo "  Interface: $INTERFACE"
echo "  Gateway: $GATEWAY"
echo "  DHCP Range: $DHCP_RANGE_START - $DHCP_RANGE_END"

# Start dnsmasq with calculated parameters
exec /usr/bin/dnsmasq \
    -x /run/dnsmasq.pid \
    -i "$INTERFACE" \
    --dhcp-option=3,"$GATEWAY" \
    --dhcp-range="$DHCP_RANGE_START","$DHCP_RANGE_END","$LEASE_TIME" \
    -p 100 \
    -k
