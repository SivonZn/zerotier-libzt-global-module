# Routing model

`zt-globald` allocates table `51820` by default and installs one route for each
IPv4/IPv6 managed route reported by libzt. For every destination CIDR it adds a
rule whose priority is selected at runtime:

```text
zt_rule_priority < lowest detected Android VPN priority
```

The daemon reserves three unused priorities in both IPv4 and IPv6 and keeps an ownership manifest of
installed rules, addresses, and routes for cleanup. Do not reuse its table
or priorities for unrelated rules.

## Android underlay protection (0.3.6)

The daemon reads rtnetlink LINK, ADDR and ROUTE dumps across **all tables**.
It combines usable physical-interface addresses/point-to-point peers with
direct unicast routes (not just `scope link`). This covers per-netd tables,
IPv6 and /32 or /128 host prefixes. Overlay, local-table routes, loopback,
down interfaces, VPN/tunnel/CLAT names and nonphysical link kinds are excluded.
Default and /1 prefixes are never treated as a LAN. Unknown link kinds are
excluded conservatively; vendor-specific physical kinds may need adaptation.

For an overlay priority P (normally 80), the RPDB layout is:

```text
P-1: to each physical connected prefix -> goto P+1
P:   to each managed ZeroTier prefix   -> lookup 51820
P+1: from all                          -> nop
... Android's original netd/VPN/fwmark/UID/OIF rules ...
```

`goto` and its explicit `nop` anchor are installed via native rtnetlink, not
Android shell syntax. They skip only this module's overlay selector band.
They **do not look up main** and do not copy routes into another table.
Original source/metric/gateway/device/network-mark selection is left to
Android, including when two physical networks share a subnet. If the system
physical route disappears, lookup continues in system policy, never back
through the skipped ZeroTier band. LAN-versus-VPN behavior remains Android's
existing behavior: this is not a new VPN bypass or a forced Wi-Fi policy.

Manual P must be 2–32764 and the three-priority band must be free and above
detected VPN rules at daemon startup. If unavailable, the daemon stays alive
but refuses routing with `lastError`; change the priority and restart.
Do not reuse the band with other modules. Runtime priority reassignment is
not implemented; a new external rule inserted *before* the band can still
take precedence (normal RPDB behavior).

Dump timeout, kernel error, truncation, interrupted dump or an interleaved
network notification invalidates the entire probe without overwriting its
cache. On probe failure, existing module routes/selectors are withdrawn and
retried (normally after five seconds); Planet and daemon stay active. An
empty valid snapshot is different from a failed probe. Netlink rule events,
buffer loss and route/address/link events request resynchronization; the
low-frequency fallback remains.

During changeover all old overlay selectors are removed and their absence
verified **before** removing old protection. New anchors/bypasses precede
addresses/routes/selectors. Installation stops on the first failure and
withdraws partial overlay state. There may be a brief ZeroTier traffic pause;
the module does not deliberately expose an unprotected overlay selector.
If selector withdrawal cannot be verified, protections and the recovery
manifest are retained. Ownership cleanup also covers the new native rules.
zt0 addresses use `noprefixroute`, so they do not leak automatically created
connected routes into main outside the module's RPDB policy.

Full-tunnel mode is unsupported. Before any address/route installation, the
daemon validates the combined managed-route and assigned-address CIDRs for
both IP families. It rejects /0, /1, invalid CIDRs, and smaller prefixes whose
union completely covers either /1 (e.g. four /2 routes). A prefix trie avoids
double-counting overlaps and duplicate entries. Noncanonical host bits cannot
bypass the prefix-length check.

On rejection the entire snapshot is refused, the previous owned routing
entries are withdrawn, and `lastError` explains the policy violation. The
daemon and Planet stay active. Removing the offending controller settings
allows the next synchronization to restore routing. This prevents complete
split-default coverage; it is not a trust boundary against a controller that
targets selected public networks or leaves deliberate coverage gaps. An
explicit destination allowlist would be needed for that stronger guarantee.

No DNS- or mDNS-specific routes, socket-mark rules, resolver interfaces, or
default routes are installed by the module.
