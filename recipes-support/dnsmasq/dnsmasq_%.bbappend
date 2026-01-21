inherit systemd

# When softap feature is enabled, dnsmasq will be managed by wifi_ap_init
# via dnsmasq_wrapper.sh which provides dynamic DHCP range configuration
# based on the AP IP address from /etc/wifi/ap_config

do_install:append(){

    if ${@bb.utils.contains("DISTRO_FEATURES", "softap", "true", "false", d)}; then
        # Disable dnsmasq.service - wifi_ap_init will start dnsmasq via wrapper
        # This allows dynamic DHCP range based on AP IP configuration
        sed -i 's/After=network.target/After=wifi-ap.service network.target/g' ${D}${systemd_unitdir}/system/dnsmasq.service
        
        # Remove ExecStart and add one that does nothing (Type=oneshot with no exec)
        # Or alternatively, mask the service behavior
        sed -i '/ExecStart=/d' ${D}${systemd_unitdir}/system/dnsmasq.service
        
        # Add a dummy ExecStart that exits immediately - actual dnsmasq is started by wifi_ap_init
        sed -i '/test/a ExecStart=/bin/true' ${D}${systemd_unitdir}/system/dnsmasq.service
        sed -i 's/Type=dbus/Type=oneshot/g' ${D}${systemd_unitdir}/system/dnsmasq.service
        sed -i '/RemainAfterExit/d' ${D}${systemd_unitdir}/system/dnsmasq.service
        sed -i '/Type=oneshot/a RemainAfterExit=yes' ${D}${systemd_unitdir}/system/dnsmasq.service
    fi
}