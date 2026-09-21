# Linker, MTU and Android underlay review (0.3.5)

## Relative library search path

`src/CMakeLists.txt` already links a PIE. Its output has no RUNPATH; supported
entry points (`service.sh` and `zt-globalctl`, used by uninstall) export the
module-private lib directory as `LD_LIBRARY_PATH`. An `$ORIGIN/../lib` RUNPATH
is therefore optional for these flows, but useful for direct invocation or
environments that clear this variable. Android API 24+ supports DT_RUNPATH
and `$ORIGIN`; the daemon currently targets API 24. It does not bypass linker
namespaces, filesystem permissions or SELinux. If added, verify the literal
RUNPATH with NDK llvm-readelf; `$ORIGIN` must reach the linker unexpanded.
This review does not change the link options.

## TCP MSS and MTU

The local kernel knows zt0's configured MTU (default 1400). Ordinary TCP
originated by the phone already derives its MSS from that route: normally
1360 for IPv4, 1340 for IPv6 before accounting for extra header options.
Lack of an explicit TCPMSS rule does not by itself prove a PMTU black hole.

There are two different limits: inner IP over TAP and encrypted UDP over the
physical path. Bundled Core fragments its packets in Switch.cpp using path
MTU / `ZT_DEFAULT_PHYSMTU` (1432-byte ZeroTier UDP payload by default).
Phy.hpp disables IP/IPV6 MTU discovery where the platform exposes the socket
option and permits fragmentation. Small underlays and fragment-filtering
middleboxes can therefore cause a fragmentation/drop problem; do not assume
healthy outer-path PMTUD merely because inner TCP has an MSS.

`--clamp-mss-to-pmtu` on zt0 consults the inner kernel route, not libzt's outer
peer path, and cannot automatically discover its smaller MTU. Clamping can
help forwarded TCP or paths with filtered ICMP, but does not fix UDP/QUIC,
already-established TCP sessions, or libzt control packets. Start with
captured SYN MSS, inner/outer packet sizes, fragmentation and ICMP evidence
on the affected mobile path. Consider a measured TAP MTU and a libzt outer
path size budget (including UDP/IP/overlay headers); any optional MSS rules
should be scoped to zt0/new SYN and SYN-ACK traffic, cover local and forwarded
traffic as appropriate, and participate in owned-rule cleanup. No firewall
rules are added in this change.

## Underlay detection on Android

The pre-0.3.6 `queryUnderlayCidrs` had these concrete limitations:

- It reads only main, while netd may keep WLAN/cellular routes in per-network
  tables selected by fwmark/UID policy. Merely looking up main can fall through
  to the next rule, which is the higher-priority ZeroTier destination rule.
- `cidr.find(':')` also matches IPv6 addresses and consumes the next token.
- `scope link` excludes many connected IPv6 global prefixes; bare IPv4 host
  routes (/32 without a slash in ip output) are skipped too.
- Query errors are indistinguishable from an empty result; a transient error
  can remove protection entries on the next synchronization.
- Only prefixes are retained, losing original table/interface/source/metric
  selection. Interface-name filtering alone cannot identify every VPN or CLAT.

An overlapping WLAN CIDR is enough to lose LAN connectivity; rejecting /1
does not prevent a managed 192.168.x.0/24 route from causing this problem.
No current device reproduction is claimed: 192.168.1.114 ADB timed out during
this review. These findings are based on the implementation and Android's
policy-routing model.

Recommended follow-up: use rtnetlink route/address/link dumps across tables,
retain RTA_TABLE, OIF, gateway, source and metric, exclude the module's own
table/zt0 and classify physical networks with netd network state where
available. Build explicit protection routes in a module-owned table, or
table-specific exceptions, and verify a route exists before enabling each
exception. Resolve overlapping multiple physical networks explicitly rather
than indiscriminately copying every table. Failed/incomplete dumps should
retain the last valid protection or withdraw conflicting overlay routes;
apply a new protection set before deleting old protection. Validate under
Wi-Fi/cellular handover, IPv6, CLAT, VPN and LAN/ZeroTier prefix overlap.

### Implemented in 0.3.6

The follow-up now uses native multi-table dumps and physical address prefixes.
Instead of copying physical routes or pinning table-specific exceptions, a
`goto`/`nop` band skips only module-owned overlay selectors. Consequently the
kernel's original netd rules retain all route metadata and network selection;
there is no need to duplicate source, gateway or metric in module state.
Failed dumps withdraw overlay takeover, and rule transitions withdraw old
selectors before replacing their protection. See `ROUTING.md` for the full
policy, failure behavior, and compatibility boundaries. MSS and linker
behavior described above is unchanged.
