# Troubleshooting

Useful commands (run as root):

```sh
/data/adb/modules/zerotier-libzt-global/zt-globalctl status
/data/adb/modules/zerotier-libzt-global/zt-globalctl routes
/data/adb/modules/zerotier-libzt-global/zt-globalctl multicast
cat /data/adb/zt-global/logs/daemon.log
cat /proc/net/dev_mcast | grep ' zt0 '
ip rule
ip route show table 51820
ip link show zt0
```

The module requires `/dev/net/tun`, `ip`/`ip6tables` tooling, and a kernel
that permits creating a TAP interface. TAP is the only supported interface
type; kernels that reject TAP cannot run this module. `/dev/net/tun` is the
shared Linux device used to create TAP interfaces, despite its name.

The module intentionally provides no DNS or mDNS integration. A `.local`
lookup failure is therefore expected; use the peer's ZeroTier IP address or
configure an external DNS service for the ZeroTier network.
