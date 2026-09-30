# Flashing from WSL2 (usbipd-win)

> [!WARNING]
> This is unreliable with current usbipd-win.

See:

- <https://github.com/dorssel/usbipd-win/issues/924>
- <https://github.com/dorssel/usbipd-win/issues/1022>
- <https://github.com/dorssel/usbipd-win/issues/1067>

On Windows, QDL can run inside a WSL2 distribution, but WSL2 does not see USB
devices by default - they must be forwarded from the Windows host with
[usbipd-win](https://github.com/dorssel/usbipd-win). Install it on the Windows
side (`winget install usbipd`), then forward the EDL device into WSL.

EDL devices enumerate under Vendor ID `05c6`, most commonly with Product ID
`9008` (Firehose) or `900e` (crash dump). From a Windows terminal, list the
connected devices and note the **BUSID** of the EDL device:

```powershell
usbipd list
```

Each device must be *bound* once before it can be attached. Binding requires an
**elevated (Administrator)** PowerShell, but it persists across reboots, so this
is a one-time step per device:

```powershell
usbipd bind --busid <BUSID>
```

Attaching the device to WSL does **not** require Administrator and can be run
from inside WSL by calling the Windows binary:

```bash
usbipd.exe attach --wsl --busid <BUSID>
```

Once attached, the device appears in WSL (check with `lsusb` for a `05c6:`
device) and QDL can flash it as usual.

## Re-attaching after EDL re-enumeration

The EDL device is a USB gadget that only exists while the board is in EDL mode.
Whenever the board re-enters EDL - including after QDL finishes and the target
reboots - the USB device is torn down and re-created. The `bind` persists, but
the **attach is dropped**, and the host may assign a **different BUSID** than
before. A QDL run that reports no device found is almost always this: the device
simply needs to be re-attached.

After each entry into EDL, re-detect the BUSID and attach again. This can be
scripted from WSL so the BUSID does not have to be looked up by hand:

```bash
BUSID=$(usbipd.exe list | grep -i '05c6:' |
        awk '{print $1}' | tr -d '\r')
usbipd.exe attach --wsl --busid "$BUSID"
```

If you flash repeatedly, run this re-attach step before every `qdl` invocation.
