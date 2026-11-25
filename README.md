# atbm-wifi

The Linux driver for AltoBeam's ATBM6162 USB WiFi (`007a:6162`). It is
AltoBeam's WiFi 6 SDK as packaged by
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
- The chip is reset only directly before a firmware download, and only
  when it answers over USB.
- USB transfer errors that mean the device is leaving the bus stop the
  transfer instead of restarting the firmware; control transfers hold one
  buffer at a time and fail at once after the device has gone.
- Receive rules from upstream mac80211: extended channel switches in
  beacons, ADDBA retransmissions and TSPEC TIDs, robust action frames
  before association, EAPOL frames to another address.
- No compiler warnings with the kernel's default warning set.

## Firmware restarts and chip resets

A firmware fault (a command or TX confirmation that does not come, a
firmware exception, a broken receive stream) restarts the firmware:
mac80211 takes the interface out of the driver, the chip is reset and
the firmware downloaded again, and an associated
station reports a connection loss, after which wpa_supplicant
reconnects. A command waits 3 s for its confirmation (7 s for JOIN and
START), then up to 10 s from when it was sent before the restart.

The driver logs its state when the firmware halts, and each step of the
restart, at warning level, so the console record kept across a warm
reboot shows how far a restart got.

The driver does not assume it can remove the chip's power or drive a
reset line. A chip whose CPU, HIF and MAC have been reset, with no
firmware downloaded since, does not come back after the next USB bus
reset (a warm reboot, a port reset) until its power is removed. So the
driver resets the chip only directly before a firmware download, in the
same USB session:

- At probe, after it has fetched and checked the firmware image. A failed
  download or firmware startup is retried, reset and all, up to five
  times with a growing wait.
- In a firmware restart, half a second after it starts, and only if the
  chip answers a control read, tried up to three times a second apart. A
  device that has left the bus by then, or a chip that does not answer,
  is left as it is, running its firmware. The download is tried up to
  three times, each after a fresh reset.

The restart holds RTNL and the wiphy lock from stopping mac80211 to
restarting it, so nothing reaches the driver while it reloads the
firmware; each download is abandoned after 20 s, and no attempt starts
after 60 s.

While the chip is reset without firmware, after a probe or a restart
that failed, the driver unbinds and probes the interface again itself,
0.5 s later, then after 2, 5, 10 and 30 s, then every minute: a USB bus
reset in that time, such as the hardware reset of a reboot whose
shutdown outlasts a watchdog, detaches the chip. A restart that fails
without having reset the chip does the same, 5 s later and twice as
long after each failure in a row, up to 5 min. The
driver logs which of the two a failed restart left. Once a reboot or
power-off starts, neither a probe nor a restart starts, and a chip still
reset without firmware is reset and downloaded once more, so the reboot
finds it running: by the driver's shutdown hook when it is bound, and by
a probe from its reboot notifier when it is not.

No bulk transfer error restarts the firmware. A transfer that the host
cancelled or whose device has gone stops. A failed transmit is left to
its confirm, or to the command or confirm timeout if the device never
took it. A stalled endpoint is cleared, after that direction's other transfers
are cancelled. A failed receive is read again, up to three in a
row; after that the receive URB stops, and once the chip answers a
control read the driver submits the stopped receive URBs again, without
a reset, a second later and twice as long after each refill with no good
frame in between, up to 30 s.

While a restart has failed, the driver logs `firmware restart failed:
WiFi is off` and sets the wiphy's hardware rfkill block
(`/sys/class/rfkill/rfkillN/hard` reads 1): the interface goes down and
cannot be brought up until the driver probes the chip again. A control
transfer is tried three times unless the device has left the bus, and a
register write that depends on a read is skipped when the read fails. A USB reset
of the device unbinds the driver and probes it again. Unloading the
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

The station configuration is `configs/atbm6162u_station.config`, copied
to `.config` in this tree. Against a configured kernel tree:

```sh
cp configs/atbm6162u_station.config .config
make -C <kernel build dir> ARCH=<arch> CROSS_COMPILE=<toolchain prefix> M=$PWD modules
```

The module is `hal_apollo/atbm6162.ko`. It is built for the WiFi and
BLE combination firmware: install
`firmware/firmware_usb_wifi_bt_comb_ocea.bin` as
`/lib/firmware/atbm6162_fw.bin` and load the module with
`wifi_bt_comb=1`. The build is reproducible: it embeds no build time.

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
| `4` | fail every bulk-in transfer with `-EPROTO` for two seconds |
| `5` | fail every bulk-out transfer with `-EPROTO` for two seconds |
| `6` | restart the firmware, with the chip taken as not answering its control read |
| `7` | restart the firmware, with the restart's first download failing |
| `8` | fail the next five firmware downloads, every attempt of a probe; can be given at load |
| `9` | restart the firmware, with all of the restart's downloads failing |
| `10` | drop the next startup indication, so a download fails after its receive URBs were posted; restarts the firmware, or applies to the next probe when given at load |

Other builds have no such parameter.

## Licences

The driver is GPL-2.0, in `LICENSE`. The firmware in `firmware/` is
AltoBeam's proprietary binary, as AltoBeam ships it in its SDK. The
Apache NimBLE stack in `ble_host/` keeps its own `LICENSE`.
