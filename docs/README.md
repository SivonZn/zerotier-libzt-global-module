# ZeroTier libzt Global Route

This Magisk/APatch module runs a root `zt-globald` process, starts libzt with
its embedded official ZeroTier Planet by default, optionally applies a custom
Planet/Roots override, joins one ZeroTier network, and mirrors that network's
managed routes into an isolated Linux routing table. Rules for those CIDRs are
installed with a priority numerically smaller than Android VPN rules.

The default `route_mode=managed` only changes traffic destined for CIDRs
advertised by the ZeroTier controller. It does not replace Android DNS, the
default route, or any VPN service.

The kernel interface is always TAP, carrying Ethernet frames to and from
libzt. There is no interface-mode setting. Upgrades remove the obsolete
`interface_mode` key; old configurations loaded directly also use TAP.
The control command accepts `save ENABLED NETWORK_ID PRIORITY TABLE MTU PORT`.

The TAP multicast membership seen in `/proc/net/dev_mcast` is synchronized
into libzt and merged with libzt's own ARP/NDP groups. The five-second Core
subscription cycle publishes only the set difference, preserving ordinary L2
multicast behavior required by TAP without implementing name resolution.

The module does not modify Android DNS, Private DNS, DnsResolver, the default
network, or VPN network selection. It does not provide mDNS or `.local` name
resolution; use IP addresses or a DNS service managed outside this module.

Configure `/data/adb/zt-global/config.ini`, then reboot or run
`zt-globalctl restart` as root. The package currently targets arm64-v8a;
additional ABIs can be added by placing matching binaries under the module's
private `bin` directory and libraries under its private `lib` directory.

The current installer accepts only `arm64` / Android `arm64-v8a` userspace.
Other or undetectable architectures are rejected before persistent data is
modified; supporting another ABI also requires updating this installer gate.
Configuration saves update only `enabled`, `network_id`, `rule_priority`,
`routing_table`, `mtu`, and `port`, preserving comments, unknown keys and
non-UI settings. A temporary file and atomic rename protect the existing
file from failed writes. The obsolete `interface_mode` key is removed.

The APatch/KernelSU WebUI can install a custom Planet from the Network page.
The selected binary is uploaded in bounded chunks, verified on-device, and
atomically written to `/data/adb/zt-global/planet`; the daemon then restarts
and treats that file as authoritative, falling back to `roots_path` only when
no Planet file is installed. The same page can reset this override: it removes
both the custom `planet` and legacy `roots` fallback, then restarts the daemon
so libzt uses the official Planet embedded in the bundled ZeroTier core.

Planet activation succeeds when the restarted daemon confirms local Core
initialization and, for a custom Planet, an exact match with the world Core
actually loaded. `planetLoaded` is independent of `nodeOnline`, controller
authorization, assigned addresses, and `dataPlaneReady`. Offline nodes keep
trying; authorization has no deadline. An empty Network ID permits Planet
initialization without joining a network.

`planet-commit` now performs the restart and confirmation itself; clients must
not restart again. Both upload and official reset use the same serialized
transaction. The previous `planet` and `roots` (including their absence) are
backed up under `planet-transaction/` before replacement. Only local loading
or startup failure triggers rollback. A rollback is acknowledged only after
the previous Planet loads; interrupted/failed recovery retains its backup
and resumes on `zt-globalctl restart` or the next boot. The 35-second local
startup bound is not a network-connectivity timeout. Status acknowledgements
include a per-start activation token, so an old daemon cannot confirm a new
switch. Legacy data-plane-based rollback markers are retired.

The bundled libzt additionally exports `zts_node_get_loaded_planet`, returning
the serialized boot-time world accepted by Core after local initialization.
Rebuild both libzt (`./build_libzt.sh`) and daemon (`./build.sh`) when updating
this extension; an older libzt is not compatible with the new daemon.

Build tools resolve the NDK from `ANDROID_NDK_HOME`, then `ANDROID_NDK_ROOT`,
or the latest numbered side-by-side NDK in `ANDROID_SDK_ROOT` / `ANDROID_HOME`.
Without explicit paths, standard SDK locations (`$HOME/Library/Android/sdk`
on macOS, `$HOME/Android/Sdk` on Linux) are checked. Set `ANDROID_NDK_HOME` to
pin a version for reproducible builds. libzt headers and binaries come only
from this module; no external development checkout is required.

`build.sh`, `build_libzt.sh`, and `package.sh` use the selected NDK's
`llvm-strip --strip-unneeded` on runtime copies before packaging. Dynamic
symbols needed by Android's linker are retained. Unstripped build outputs
remain under `build/` for debugging (until the next clean build). Packaging
therefore also requires an NDK; missing tools stop packaging with an error.

On a first installation (no existing `/data/adb/zt-global/config.ini`), the
installer removes stale `planet` and `roots` override files before creating the
default configuration, guaranteeing an official-Planet startup. Module
upgrades preserve an existing configuration and its selected custom Planet.

The libzt build used by this module must expose the virtual-network frame
adapter described in `src/zt_frame_adapter.h`. The stock public ZeroTier
Sockets API provides node and route control but no kernel TAP bridge, so the
adapter is built as part of this module's native target.
