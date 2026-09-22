# libzt patches

Upstream: https://github.com/zerotier/libzt.git

Pinned commit: `a707ea6ae0910efdc1125d04758c411e2e9ea4f9`.
The parent gitlink and `base-revision` must agree. Upstream itself pins
ZeroTierOne, lwIP and lwIP-contrib; initialize them recursively.

Apply patches in `series` order:

1. `0001-android-native-only.patch`: make Android NDK-only builds avoid JNI.
2. `0002-tap-routing-multicast-planet.patch`: existing Ethernet frame bridge,
   CIDR prefixes in address/route queries, merged kernel/external multicast
   memberships, and local Core Planet-load introspection.
3. `0003-exclusive-external-tap.patch`: makes the registered external frame
   callback the sole Ethernet receive owner, isolates callbacks by network ID,
   and disables lwIP link output in the Android external-TAP build. This keeps
   the embedded stack available for Core lifecycle support without mirroring
   inbound frames or emitting competing ARP/NDP/TCP traffic.

4. `0004-identity-key-validation.patch`: derives both DH and signing public
   keys from the private key and compares them. Applies to the exported
   ZeroTierOne C25519 header and libzt Controls.cpp.

These are local customizations, not an upgrade of libzt/Core. Original
recursive vendor submodules remain clean; patches apply only under build/.

`bash tools/prepare-libzt.sh` validates the pinned clean submodules, exports
tracked upstream files to `build/`, checks every patch, and applies them to
`build/libzt-source/`. It never applies patches to `vendor/libzt-src/`.
Export-ignored upstream examples/tests/metadata are not needed for the
Android NDK-only build. Patch inputs are fingerprinted; changed inputs use
a new native build directory to prevent old object files being reused.

To update upstream: explicitly update the gitlink and `base-revision`, rebase
the patch series, then run patch/build/regression tests. Do not edit the
submodule in place or use `submodule update --remote` for normal builds.
Normal build scripts require initialization but never fetch newer commits.
