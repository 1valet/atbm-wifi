# atbm-wifi

The Linux driver for AltoBeam's ATBM6162 USB WiFi (`007a:6162`). It is AltoBeam's WiFi 6 SDK as packaged by
[thingino](https://github.com/gtxaspec/atbm-wifi), ported to Linux 7.3 and
built as a station-only driver.

The driver carries its own copy of mac80211 (`hal_apollo/mac80211`) on top
of the kernel's cfg80211. The chip runs AltoBeam's firmware
(`firmware/firmware_usb_wifi_bt_comb_ocea.bin`, installed as
`atbm6162_fw.bin`), which does the 802.11 MAC and encryption.

## What it supports

| | |
| --- | --- |
| Bands | 2.4 GHz and 5 GHz, 802.11ax; DFS channels are scanned passively |
| Interface | Station only (`NL80211_IFTYPE_STATION`) |
| Security | WPA2-Personal, WPA3-Personal (SAE), WPA2/WPA3-Enterprise, with CCMP-128 and BIP (802.11w). No WEP, TKIP or GCMP: the firmware has no GCMP key type |
| Management frames | User space sends and receives action frames on the operating channel (`mgmt_tx`), so wpa_supplicant answers 802.11k and 802.11v requests |
| Power save | Follows `CONFIG_CFG80211_DEFAULT_PS` |

## Changes from the vendor driver

The series on `main`, in order:

- The port to Linux 7.3, with interface work running as wiphy work under
  the wiphy lock, as upstream mac80211 does, and the vendor features a
  station does not use left out.
- Logging at the kernel's levels, rate limited per call site, and the
  removal of the wireless-extensions code and dead code.
- Bounds on everything read from received frames, USB transfers and
  firmware messages.
- Packet-number (replay) checks on the host for CCMP and group traffic, BIP
  verification, and one classification of received frames shared by the
  802.11 and firmware-converted 802.3 paths, with upstream's duplicate and
  reorder rules.
- Pairwise rekeying as an explicit transition: each frame belongs to the
  key whose install the firmware had confirmed when it arrived.
- Ownership rules for the TX queue and its confirmations, scans, work
  items and the connection state, and USB probe, unplug and unload paths
  that fail and tear down without leaking or hanging.
- Host tests for the receive rules.
- Firmware commands and lost TX confirmations time out in seconds, and a
  firmware restart that fails takes the radio out of service where user
  space can see it.
- The chip is reset only directly before a firmware download.

## Firmware restarts and chip resets

A firmware fault (a command or TX confirmation that does not come, a
firmware exception, a broken receive stream) restarts the firmware:
mac80211 takes the interface out of the driver, the chip is reset and
the firmware downloaded again, and an associated station reports a
connection loss, after which wpa_supplicant reconnects. A command waits
3 s for its confirmation (7 s for JOIN and START), then up to 10 s from
when it was sent before the restart. If the restart fails, the driver
logs that the radio is off until the board's power is cycled and sets
the wiphy's hardware rfkill block (`/sys/class/rfkill/rfkillN/hard`
reads 1): the interface goes down and cannot be brought up.

The chip stays powered when the SoC resets. If its CPU, HIF and MAC have
been reset and no firmware has been downloaded since, the next USB bus
reset (a warm reboot, a port reset) leaves it off USB until its power is
removed. So the driver resets the chip only directly before a firmware
download, in the same USB session: at probe, after it has fetched and
checked the firmware image (and up to three times if the download or
the firmware's startup fails), and in a firmware restart. Unloading the
driver leaves the chip running its firmware, as a warm reboot does.

## Branches and releases

| Branch | |
| --- | --- |
| `vendor` | thingino's tree at the commit this fork starts from |
| `main` | the series above, on `vendor` |

A release is a tag `vX.Y.Z` on `main` with a source tarball,
`atbm-wifi-X.Y.Z.tar.gz`, attached as a release asset, for a build system
such as Buildroot to fetch and check by hash.

To make one:

```sh
git tag -a vX.Y.Z -m vX.Y.Z
git archive --format=tar.gz --prefix=atbm-wifi-X.Y.Z/ -o atbm-wifi-X.Y.Z.tar.gz vX.Y.Z
git push origin vX.Y.Z
gh release create vX.Y.Z atbm-wifi-X.Y.Z.tar.gz --title vX.Y.Z --notes "..."
sha256sum atbm-wifi-X.Y.Z.tar.gz
```

## Building

The build configuration is the system's `package/atbm-wifi/atbm6162.config`,
copied to `.config` in this tree. Against a configured kernel tree:

```sh
cp <system>/package/atbm-wifi/atbm6162.config .config
make -C <kernel build dir> ARCH=arm64 CROSS_COMPILE=<toolchain prefix> M=$PWD modules
```

The module is `hal_apollo/atbm6162.ko`. The build is reproducible: it
embeds no build time.

## Testing

The receive rules build and run on the host:

```sh
make -C test/rx_class check
```

A test build with `ATBM_TEST_RESTART=y` added to `.config` has a
write-only module parameter, `test_restart`
(`/sys/module/atbm6162/parameters/test_restart`):

| Value | |
| --- | --- |
| `1` | restart the firmware, as a fault does |
| `2` | restart the firmware and fail the restart before anything is sent to the chip, to test the failed-restart report |
| `3` | fail the next firmware download once, after probe has reset the chip, to test the probe retry; can be given at load (`modprobe atbm6162 test_restart=3`) |

Other builds have no such parameter.

## Licences

The driver is GPL-2.0. The firmware in `firmware/` is AltoBeam's
proprietary binary.
