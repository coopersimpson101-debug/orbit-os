# orbit-chainloader serial protocol

The chainloader lets you iterate on the kernel without moving the SD card
back and forth. Put `chainloader.img` on the card once, under the name the
firmware boots (`kernel_2712.img` or `kernel8.img`). After that, every reset
waits for a kernel over the debug UART.

## Sequence

```
device                                   host (tools/send_kernel.py)
------                                   ---------------------------
"[orbit-chainloader] ready. load=..."  ->
0x03 0x03 0x03                         ->  (break signal: "send me a kernel")
                                       <-  "OBK1"  u32 len (LE)  u32 crc32 (LE)
"OK"  (or "SZ" if len == 0 or too big) ->
                                       <-  len payload bytes
"OK"  (or "CK" on CRC mismatch)        ->
"[orbit-chainloader] CRC OK, jumping"  ->  kernel output follows
```

* CRC-32 is the IEEE 802.3 reflected polynomial `0xEDB88320`, which is the same
  as Python's `zlib.crc32` (check value `crc32("123456789") = 0xCBF43926`).
* Bytes before the magic are ignored, so line noise or a terminal's stray
  keystrokes do no harm. After `SZ` or `CK` the device prints the banner and
  sends the break signal again, so the host can simply retry.
* The maximum size is `CHAIN_BASE - load_addr - 64 KiB` (about 31 MiB on both
  boards).
* The kernel is entered at the original load address with the firmware's
  `x0` (DTB), `x1 = x2 = x3 = 0`, at the entry exception level, with the
  MMU off, after `IC IALLU`. This matches the state the firmware would
  have left.

The device-side state machine is `kernel/lib/chainproto.c`. It is shared with
the chainloader and unit-tested in `tests/test_chainproto.c`, including a frame
produced by the Python sender.

## Usage

```sh
# Real hardware: USB-serial adapter (3.3 V) on the Pi 5 debug UART header
pip install pyserial
python tools/send_kernel.py --port /dev/ttyUSB0 build/rpi5/kernel8.img --monitor

# QEMU: serial port exposed as a TCP server
qemu-system-aarch64 -M virt,gic-version=2,virtualization=on -cpu cortex-a72 -m 512M \
    -display none -kernel build/virt/chainloader.img \
    -serial tcp:127.0.0.1:4555,server=on,wait=on &
python tools/send_kernel.py --tcp 127.0.0.1:4555 build/virt/kernel8.img --monitor
```

At 115200 baud a ~70 KiB kernel takes about 6-7 seconds to transfer.
