# Running two Astromechanics focusers on one host

Both units enumerate as identical USB-serial adapters and answer the same
`P#` handshake, so with defaults they collide:

* the driver's default device name is always `Astromechanics FOC`;
* both instances default to `/dev/ttyUSB0`, and auto-search cannot tell the units apart;
* `/dev/ttyUSBn` numbering can swap between boots.

## 1. Give each USB port a stable name (udev)

Find each unit's attributes (plug in one at a time):

    udevadm info -a -n /dev/ttyUSB0 | grep -E 'idVendor|idProduct|serial|KERNELS' | head

If the adapters report distinct serial numbers, match on `ATTRS{serial}`;
otherwise match on the physical port path (`KERNELS`, e.g. `1-1.2`) and always
plug each focuser into the same USB port.

`/etc/udev/rules.d/99-astromechanics.rules` (example values - replace with yours):

    # by serial number
    SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", ATTRS{serial}=="SERIAL_A", SYMLINK+="focuser_main"
    SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", ATTRS{serial}=="SERIAL_B", SYMLINK+="focuser_guide"
    # or by physical USB port
    # SUBSYSTEM=="tty", KERNELS=="1-1.2:1.0", SYMLINK+="focuser_main"
    # SUBSYSTEM=="tty", KERNELS=="1-1.3:1.0", SYMLINK+="focuser_guide"

Reload: `sudo udevadm control --reload && sudo udevadm trigger`, then check `ls -l /dev/focuser_*`.

## 2. Run two driver instances with distinct names

Each instance needs its own device name, otherwise clients see only one
`Astromechanics FOC`. With indiserver's FIFO interface:

    mkfifo /tmp/indififo
    indiserver -f /tmp/indififo &
    echo 'start indi_astromechfoc -n "Astromechanics Main"'  > /tmp/indififo
    echo 'start indi_astromechfoc -n "Astromechanics Guide"' > /tmp/indififo

Or set `INDIDEV` per instance. In Ekos/StellarMate, give each focuser instance
its own label in the profile so the names stay stable.

## 3. Configure each instance

* Connection tab: turn **Auto Search** off and set the port to `/dev/focuser_main`
  or `/dev/focuser_guide` respectively.
* Save configuration (Options -> Configuration -> Save) for each device.
