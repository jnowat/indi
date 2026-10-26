# Running two Astromechanics focusers on one host

Both units enumerate as identical FTDI USB-serial adapters and answer the same
`P#` handshake, so with defaults they collide: the driver's default device name
is always `Astromechanics FOC`, both instances default to `/dev/ttyUSB0`, and
auto-search cannot tell the units apart.

Tested on StellarMate OS 1.8.8, INDI 2.1.3, KStars 3.8.5.
Forum write-up: https://forum.indilib.org/t/two-identical-astromechanics-focusers-via-a-remote-stellarmate-the-kstars-custom-driver-alias-isnt-enough/14076

## Recommended fix: remote StellarMate / INDI Web Manager

When Ekos connects to a **remote** INDI server (StellarMate, or any
`indiwebmanager` host), a custom driver alias created in KStars
(*Custom Drivers*) exists **only on the client**. The Web Manager on the server
does not know that driver label, so it silently skips the second focuser and
only one `Astromechanics FOC` ever appears.

The fix is to define the second driver **on the server**:

1. On the StellarMate/INDI host, create `/usr/share/indi/astromech_foc2.xml`:

   ```xml
   <?xml version="1.0" encoding="UTF-8"?>
   <driversList>
       <devGroup group="Focusers">
           <device label="Astromechanics FOC 2" manufacturer="Astromechanics">
               <driver name="Astromechanics FOC 2">indi_astromechfoc</driver>
               <version>0.2</version>
           </device>
       </devGroup>
   </driversList>
   ```

   (Same binary, `indi_astromechfoc`; only the label/name differ.)

2. Restart the Web Manager (or simply reboot the StellarMate) so it rereads the
   driver XML files.
3. In the Ekos profile, list **both** `Astromechanics FOC` and
   `Astromechanics FOC 2` as focusers.
4. Connect, then in each device's *Connection* tab:
   * turn **Auto Search off**;
   * set the port to that unit's own stable by-id path, e.g.
     `/dev/serial/by-id/usb-FTDI_FT230X_Basic_UART_XXXXXXXX-if00-port0` for unit 1 and
     `/dev/serial/by-id/usb-FTDI_FT230X_Basic_UART_YYYYYYYY-if00-port0` for unit 2
     (placeholders - use `ls -l /dev/serial/by-id/` to find yours);
   * baud rate **38400**;
   * *Options -> Configuration -> Save*.

The by-id paths embed each FTDI chip's serial number, so the mapping survives
reboots and replugging.

## Fallback: udev aliases

If your adapters do not expose distinct serials under `/dev/serial/by-id`, or
you are running indiserver by hand, the following still applies.

### 1. Give each USB port a stable name (udev)

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

### 2. Run two driver instances with distinct names

Each instance needs its own device name, otherwise clients see only one
`Astromechanics FOC`. With indiserver's FIFO interface:

    mkfifo /tmp/indififo
    indiserver -f /tmp/indififo &
    echo 'start indi_astromechfoc -n "Astromechanics Main"'  > /tmp/indififo
    echo 'start indi_astromechfoc -n "Astromechanics Guide"' > /tmp/indififo

Or set `INDIDEV` per instance. In Ekos/StellarMate, give each focuser instance
its own label in the profile so the names stay stable.

### 3. Configure each instance

* Connection tab: turn **Auto Search** off and set the port to `/dev/focuser_main`
  or `/dev/focuser_guide` respectively.
* Save configuration (Options -> Configuration -> Save) for each device.
