# **SLSsteam - Steamclient Modification for Linux**
![](https://github.com/AceSLS/SLSsteam/blob/dev/res/banner.png?raw=true "SLSsteam")

## Linux content-pipeline fork

This AGPL-3.0-only fork is paired with
[`ccc007ccc/slssteam-manager`](https://github.com/ccc007ccc/slssteam-manager) and retains the
upstream SLSsteam installation layout. It adds the runtime pieces required for configured
manifests to enter Steam's Linux content pipeline:

- missing Depot dependency injection through `BuildDepotDependency`;
- installed-size propagation through manager-provided `ManifestSizes`;
- runtime DepotKey responses for messages 5438/5439;
- asynchronous `ContentServerDirectory.GetManifestRequestCode#1` response replacement;
- manifest ID pinning for existing and injected Depot entries.

The control-plane/runtime boundary is documented in
[`slssteam-manager/ARCHITECTURE.md`](https://github.com/ccc007ccc/slssteam-manager/blob/main/ARCHITECTURE.md).

The hooks are disabled unless `EnableContentHooks: yes` is present. The manager writes
`InjectedDepots`, `ManifestSizes`, `DepotKeys`, and `ManifestIds` together while preserving the
config inode.
Manifest request codes use OpenSteamTool-, WUDRM-, and SteamRun-compatible providers unless a
custom `ManifestCodeURL` is configured.

Depot-vector growth uses the exported `libtier0_s.so` `Plat_Realloc` wrapper. Do not call the
private `g_pMemAllocSteam` C++ vtable directly: its ABI includes destructor slots and is not a
stable allocation interface.

Build the 32-bit runtime with the included container definition. Installation continues to use
the upstream `setup.sh install` flow and only writes user-owned Steam/SLSsteam paths:

```bash
podman build -t slssteam-build -f docker/Dockerfile docker
podman run --rm --userns=keep-id \
  --mount type=bind,source="$PWD",target=/src --workdir=/src \
  localhost/slssteam-build:latest make build
```

Before installation, fully exit Steam and back up `~/.local/share/SLSsteam`, the user Steam
desktop entry, and `~/.config/SLSsteam/config.yaml`.

On SteamOS, `setup.sh install` also creates user-level systemd drop-ins for
`steam-launcher.service`, the KDE Steam autostart unit, and KDE-created Steam desktop-launch units.
This keeps `LD_AUDIT` scoped to Steam, works when launching Steam from a desktop shortcut, survives
immutable-system updates, and does not patch `/usr/bin/steam-jupiter`.

## Index

1. [Getting started](#getting-started)
2. [Hall of Fame 👑](#hall-of-fame-aka-credits)
3. [Hall of Shame 🚨](#hall-of-shame-aka-scammers-leechers-etc)
4. [Support](#support)
5. [Related Projects](#related-projects)

## Getting started

Check out the [Installation](https://github.com/AceSLS/SLSsteam/wiki/Installation) or the [Building from Source](https://github.com/AceSLS/SLSsteam/wiki/Building-from-Source) section in our Wiki!


## Hall of Fame aka Credits

Contributors:
- [Parasitic-Hollow](https://github.com/Parasitic-Hollow/): Fixing gamepad issues caused by FakeAppIds & maintaining SLSsteam in my absence
- [amione](https://github.com/xamionex/): Creating the SLSsteam banner & logo the instant he found out I was looking around for one <3
- [DeveloperMikey](https://github.com/DeveloperMikey): Added Nix support 
- [skrimix](https://github.com/skrimix): Added flatpak support
- thismanq: Informing me that DisableFamilyShareLockForOthers is possible

Others:
- All the staff members of the Anti Denuvo Sanctuary for all their hard work they do. They also found a way to use SLSsteam I didn't even intend to, so shoutout to them
- Riku_Wayfinder: Being extremely supportive and lightening my workload by a lot. So show him some love my guys <3
- Gnanf: Helping me test the Family Sharing bypass
- rdbo: For his great libmem library, which saved me a lot of development and learning time
- jbeder: For the awesome yaml-cpp library which allowed me to easily add a configuration file
- oleavr and all the other awesome people working on Frida for easy instrumentation which helps a lot in analyzing, testing and debugging
- All the folks working on Ghidra, this was my first project using it and I'm in love with it!
- And many more I can't possibly list here for reporting bugs and giving feedback! Thank you guys <3


## Hall of Shame aka Scammers, Leechers, etc

🚨This list exists purely for educational and entertainment purposes!
Please do not seek out Projects listed here!
If you decide to ignore the aforementioned warning you do so on your own risk!🚨

OnetapBeta by Hammer Steam: Resells Steamless & SLSsteam. Intellectually went far enough to rename SLSsteam to deckloader2, that's about as far as their skill extends.

## Support

Please do not treat the issue tracker like a private support hotline!
Feel free to join our [Discord](https://discord.gg/j3ZzjeV4eQ) instead.

## Related Projects

[h3adcr-b](https://github.com/Deadboy666/h3adcr-b) & [h3adcr-b-wiki](https://github.com/Deadboy666/h3adcr-b/wiki/): Universal SLSsteam installer & steamclient downgrader

[steamnetsock-patch](https://github.com/yesyes0649/steamnetsock-patch) & [eos-proxy](https://github.com/yesyes0649/eos-proxy): Makes FakeAppIds work in some games where it otherwise wouldn't

[CloudRedirect](https://github.com/Selectively11/CloudRedirect): Enables Steam Cloud, playtime & achievement sync on unowned games

[SteamDB App Parser](https://greasyfork.org/en/scripts/543010-steamdb-app-parser): Vibecoded script that adds a button on SteamDB Game's pages to generate AdditionalApps & DlcData
