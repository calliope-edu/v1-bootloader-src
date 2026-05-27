# Calliope mini v1 / v2 bootloader — BD_ADDR keep-app fix

This is a fork of [`matthewelse/microbit-bootloader`](https://github.com/matthewelse/microbit-bootloader)
(archived) — a GCC-buildable Nordic SDK-8 / S110 DFU bootloader for the
nRF51822, the same chip used on **Calliope mini v1 and mini v2**.

## Why this exists

The campus `mini-connection-widget` flashes over Web Bluetooth. Its
BLE-DFU flow for mini v1/v2 is:

1. Connect to the running codal app.
2. Write `0x01` to the legacy DFU Control characteristic
   (`e95d93b1`, service `e95d93b0`) to reboot into the Nordic DFU
   bootloader.
3. Reconnect to the bootloader and stream firmware over legacy Nordic
   DFU (`00001530-…`).

Step 3 fails on stock firmware because the stock bootloader **increments
its BD_ADDR by one** on entry (`dfu_transport_ble.c`,
`dfu_transport_update()`). Web Bluetooth pins its `BluetoothDevice`
handle to the app's MAC, so `device.gatt.connect()` can't reach the +1
address — service discovery returns *"No Services found in device"*.

iOS/Android don't hit this: Nordic's mobile DFU libraries re-scan for
`DfuTarg` by name and pair to whatever address advertises (Android uses
`DfuServiceInitiator.setForceScanningForNewAddressInLegacyDfu(true)`).
Web Bluetooth has no equivalent — it cannot re-scan and silently
re-pair to a new address without a fresh user gesture.

**The fix:** keep the bootloader's BD_ADDR identical to the app's, so
the widget reconnects to the same handle. The bootloader stays
distinguishable by its advertised name (`DfuTarg`) and service set at
the GATT layer. This is the nRF51/SDK-8 counterpart of the fix already
shipped on the nRF52 [v3-bootloader](https://github.com/calliope-edu/v3-bootloader)
(`Modify BLE address handling … maintain application MAC address`).

The change is one line, in
[`components/libraries/bootloader_dfu/dfu_transport_ble.c`](components/libraries/bootloader_dfu/dfu_transport_ble.c)
around line 1073 — the `addr.addr[0] += 1;` block is left disabled
(it was already commented in the upstream matthewelse source; the
surrounding Calliope comment documents why).

## mini v1 and mini v2 share this bootloader

Both are nRF51822 (256 KB flash, S110 SoftDevice region ending at
0x18000, app at 0x18000–0x3BBFF, bootloader at 0x3C000). Only the
*interface chip* differs — mini v1 ships DAPLink, mini v2 ships
SEGGER J-Link OB — which affects USB flashing, not the BLE bootloader.
So this one hex covers both.

mini v3 is a different chip (nRF52833) with its own modern bootloader;
see `calliope-edu/v3-bootloader`.

## Build

Modern arm-none-eabi-gcc (tested on 10.3.1). Two build-setup deltas
from the archived upstream:

- `components/toolchain/gcc/Makefile.posix` — point `GNU_INSTALL_ROOT`
  at the system toolchain (`/usr`, version `10.3.1`).
- `examples/dfu/bootloader/pca10028/single_bank_ble_s110/armgcc/Makefile`
  — add `-flto` to `CFLAGS`. Modern gcc emits ~140–200 bytes more than
  the original 4.8.3, overflowing the bootloader flash region;
  link-time optimisation recovers the space. (`--gc-sections` was
  already present.)

```bash
cd examples/dfu/bootloader/pca10028/single_bank_ble_s110/armgcc
make
# → _build/nrf51422_xxac.hex   (bootloader at 0x3C000, UICR→0x3C000)
```

The committed [`calliope_v1v2_bootloader_bd_addr_keep_app.hex`](calliope_v1v2_bootloader_bd_addr_keep_app.hex)
is this build's output.

## Provenance / verification status

**Confirmed (toolchain-independent), 2026-05:** a *stock* build of this
source (BD_ADDR increment restored) structurally matches Calliope's
shipped `microbit-samples/yotta_modules/ble-nrf51822/bootloader/
s110_nrf51822_8.0.0_bootloader.hex`:

| Property | Shipped hex | Stock rebuild |
|---|---|---|
| Bootloader flash region | 0x3C000 | 0x3C000 |
| UICR bootloader-address (0x10001014) | 0x3C000 | 0x3C000 |
| SoftDevice | S110 nRF51 | S110 nRF51 |
| Total size | 14,660 B | 14,380 B |

Vector-table addresses differ (gcc 4.8.3 original vs gcc 10.3.1 + LTO
here) — expected for the same source under a different toolchain.

**Hardware-verified (2026-05-27)** on a Calliope mini v1 (DAPLink
unique-id `12A0000…`): flashed this bootloader + makecode-1-ble app,
A+B+Reset into pairing mode (app advertised at MAC
`CD:10:74:D0:99:9D`), wrote `0x01` to `e95d93b1` to trigger DFU. The
device rebooted and re-advertised **at the same MAC `CD:10:74:D0:99:9D`**
(stock behaviour would be `…9E`), exposing the legacy Nordic DFU
service `00001530-…`. Connected to it within the DFU window and
confirmed the service set. This is exactly what Web Bluetooth needs —
`device.gatt.connect()` reaches the same handle after the reboot, so
service discovery no longer fails with "No Services found in device".

**Still not byte-verified** against the exact factory build (needs
period gcc 4.8.3) — but the fix's *behaviour* is now confirmed on
real hardware, which is what matters.

## IMPORTANT: deploy as a combined hex, not bootloader-only

Drag-flashing this bootloader-only hex on the mini v1 DAPLink
**mass-erased the app region** — the display went blank until
makecode-1-ble was re-flashed (the patched bootloader itself was
intact and the device booted fine once the app was restored). So for
deployment, bundle the patched bootloader into a **combined image**
(SoftDevice + DAL/app + bootloader) so a single flash leaves the
device fully working. A standalone bootloader flash will wipe the app.

## Deployment + recovery

To deploy the fix on existing hardware, the patched bootloader must be
written to the device (the bootloader region isn't part of the normal
MakeCode/MicroPython "partial image" flashes). Options:

1. **One-time drag-flash** of this hex (bootloader region only) via the
   DAPLink/J-Link mass-storage drive.
2. **Bundle** the patched bootloader into a full combined image
   (SoftDevice + DAL + bootloader) so a normal firmware update also
   refreshes the bootloader.

**Recovery if a flash goes wrong:** both minis have an onboard debugger
(v1 = DAPLink, v2 = J-Link OB). Drag-flash a known-good *combined* image
(SoftDevice + app + bootloader) — the mass-storage interface rewrites
flash regardless of current target state. A bad bootloader flash is
recoverable, not permanent.

## Caveats

- The upstream matthewelse build targets the nRF51-DK (`pca10028`); the
  Calliope mini may carry minor board-specific differences (display
  animation, button mapping) not present here. Validate behaviour on
  hardware before fleet deployment.
- The bootloader-settings page (0x3FC00–0x40000) layout must match what
  the running app expects. If a future app build changes it, re-init may
  be needed.
