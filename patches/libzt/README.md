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

These patches preserve the previous vendored implementation, not an upgrade
of libzt/Core. ZeroTierOne/lwIP/lwIP-contrib have no local patches.

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
