.. _User Manual:

======================================
 User Manual – AR488-ESP32 Rev5 Board
======================================

This manual describes how to use the AR488-ESP32 Rev5 GPIB adapter: connecting it
over USB or WiFi, talking to instruments, and keeping the configuration across power
cycles. It covers the firmware on branch ``rev5-dev``. The other chapters describe the
firmware in general and the other supported boards (Arduino, ESP32 devkits). For building the firmware from
source see :ref:`Building an AR488 GPIB Interface`; the full list of ``++``
commands is in the :ref:`Command Reference`.

.. contents:: On this page
   :local:
   :depth: 2


Overview
========

The adapter connects a PC to instruments on an IEEE-488 (GPIB) bus. It understands
the Prologix ``++`` command set, so most software written for a Prologix GPIB-USB or
GPIB-ETHERNET controller works with it.

.. list-table::
   :widths: 25 75

   * - Controller
     - ESP32-S2-MINI-2 module (ESP32-S2, native USB, 2.4 GHz WiFi)
   * - Bus transceivers
     - SN75160 (data) and SN75161 (control), 48 mA bus drivers
   * - Host interfaces
     - USB (virtual serial port) and WiFi (TCP port 23)
   * - Default role
     - GPIB controller-in-charge, own address 0
   * - Default target
     - instrument at GPIB address 18

The USB and WiFi consoles are equivalent: every command works on both. The adapter
answers on whichever console sent the last command.


Getting Started over USB
========================

Connect the adapter to the PC with a USB cable. No driver is needed on Linux, macOS or
Windows 10/11; the adapter appears as a standard USB serial (CDC) device:

=======  =============================================================================
Linux    ``/dev/ttyACMx`` – or, independent of plug order,
         ``/dev/serial/by-id/usb-Espressif_Systems_AR488_GPIB-USB_<serial>-if00``
macOS    ``/dev/cu.usbmodem<serial>…``
Windows  ``COMx`` (see Device Manager → Ports)
=======  =============================================================================

The adapter reports itself as **AR488 GPIB-USB**. Its USB serial number is the last
six digits of its MAC address, the same as in its host name (see `Host name`_): the
adapter ``ar488-f1b5ce`` has the serial number ``F1B5CE``. With several adapters
connected, the ``ttyACMx`` numbers depend on the order in which they were detected
and can change after a restart. Use the ``/dev/serial/by-id/`` link instead – it
always points to the same adapter. ``lsusb -v`` or the Windows Device Manager
(*Details → Device instance path*) show the serial number.

Serial settings:

- **Baud rate:** any value works, the data always moves at USB speed; use 115200.
  **Never use 1200 baud** – opening the port at 1200 baud restarts the adapter in
  firmware-update mode (see :ref:`Updating the Firmware`).
- 8 data bits, no parity, 1 stop bit, no flow control.
- **DTR must be asserted.** Terminal programs do this automatically.
- **Line end:** CR, LF, CR+LF or LF+CR – all are accepted.

On Linux, your user needs to be in the ``dialout`` group to open the port.

Example with picocom (``-c`` shows what you type):

.. code-block:: bash

   picocom -b 115200 -c /dev/serial/by-id/usb-Espressif_Systems_AR488_GPIB-USB_F1B5CE-if00

Type ``++ver`` and Enter. The adapter replies with its version::

   ++ver
   AR488 GPIB controller, ver. 0.49.16, 22/11/2026

Leave picocom with :kbd:`Ctrl-A` :kbd:`Ctrl-X`. On Windows, PuTTY or Tera Term work
the same way.


Talking to an Instrument
========================

Commands and data
-----------------

Every line you send is either

- a **command for the adapter** – it starts with ``++`` (for example ``++addr 18``), or
- **data for the instrument** – anything else (for example ``*IDN?``). It is sent to
  the instrument at the current address.

Typing and editing
------------------

Backspace (BS or DEL) deletes the last character. Characters that should be sent to
the instrument literally – CR, LF, ESC or a leading ``+`` – must be preceded by ESC
(``0x1B``).

First query
-----------

.. code-block:: text

   ++addr 18        select the instrument (GPIB address 18)
   ++auto 2         read the answer automatically after a query
   *IDN?            ask the instrument to identify itself
   ROHDE&SCHWARZ,CMS54,...

Without ``++auto 2``, request the answer explicitly:

.. code-block:: text

   *IDN?
   ++read eoi

Response termination
--------------------

The adapter has to know where an instrument's answer ends. ``++eor`` sets the
character(s) it waits for. The default is CR+LF. **Many instruments end their answers
with LF only** (for example the R&S NRVD); with the default setting the adapter then
keeps reading until the read timeout expires, which makes every query slow and leaves
an extra empty line. Match the setting to the instrument:

=========  ==============================  =========  ===========================
``++eor``  answer ends with                ``++eor``  answer ends with
=========  ==============================  =========  ===========================
0          CR+LF (default)                 4          LF+CR
1          CR                              5          ETX
2          LF                              6          CR+LF+ETX
3          (none – wait for timeout)       7          EOI signal only
=========  ==============================  =========  ===========================

``++eos`` is the counterpart for the other direction: the characters the adapter
appends to data it sends **to** the instrument (0 = CR+LF, 1 = CR, 2 = LF, 3 = none).
``++eoi 1`` additionally asserts EOI with the last byte.

Finding instruments
-------------------

``++findlstn`` lists the addresses of all instruments on the bus. It tries every
address and waits up to ``read_tmo_ms`` for each, so lower the timeout first::

   ++read_tmo_ms 200
   ++findlstn
   17 18


Saving the Configuration
========================

Settings changed with ``++`` commands are active immediately but are lost at the next
restart unless you save them::

   ++savecfg
   Settings saved.

``++savecfg`` always confirms. If the settings cannot be written it answers
``ERROR - Settings could not be saved!``.

Saved are: address, mode, ``auto``, ``eoi``, ``eos``, ``eor``, ``eot_enable``,
``eot_char``, ``read_tmo_ms``, ``verbose``, ``prompt``, the ``id`` settings, and the
WiFi settings (SSID, password, host name).

``++default`` loads the factory defaults (save them with ``++savecfg`` to make them
permanent).

Factory defaults
----------------

=================  =======  ===========================
Setting            Default  Meaning
=================  =======  ===========================
``++mode``         1        controller
``++addr``         18       instrument address
``++auto``         0        no automatic read
``++eos``          0        CR+LF appended when sending
``++eor``          0        answer ends with CR+LF
``++eoi``          0        no EOI on last byte
``++read_tmo_ms``  1200     read timeout in ms
``++verbose``      off      short answers, for programs
``++prompt``       off      no prompt
=================  =======  ===========================


.. _WiFi setup:

WiFi
====

This chapter applies to every ESP32 board built with WiFi support, not only Rev5.

The adapter joins an existing 2.4 GHz WiFi network as a client. Once set up, it
connects by itself after every power-on and reconnects automatically if the network
was gone – no USB connection is needed.

First-time setup
----------------

Do this once over USB:

.. code-block:: text

   ++wifi scan
   Scanning...
   3 networks found (* = encrypted):
   1: LabNet (-43 dBm, ch 11) *
   2: OfficeNet (-61 dBm, ch 1) *
   3: Guest (-70 dBm, ch 6)

   ++wifi ssid LabNet
   ++wifi pass my-secret-passphrase
   ++wifi connect
   Connecting to WiFi: LabNet
   WiFi connected, IP address: 192.168.5.186
   mDNS name: ar488-f1b5ce.local

   ++savecfg
   Settings saved.

SSID and password are case sensitive. The password must have 8 to 63 characters;
WPA2, WPA3 and mixed WPA2/WPA3 networks work. 5 GHz-only networks are not supported.

The password cannot be read back: ``++wifi pass`` without a value only answers
``set`` or ``not set``. This keeps it hidden from anyone on the telnet console or
looking at the screen.

.. note::

   The password is stored **unencrypted** in the adapter's flash memory. Someone with
   physical access and a USB cable can read it out. Do not use a password for the
   adapter's network that also protects other systems, or put lab instruments into a
   separate WiFi network.

Checking the connection
-----------------------

``++wifi`` (or ``++wifi status``) shows the current state::

   Host:    ar488-f1b5ce (default)
   SSID:    LabNet
   Status:  connected
   IP:      192.168.5.186
   mDNS:    ar488-f1b5ce.local
   Signal:  -43 dBm, channel 11
   Telnet:  port 23, client connected
   MAC:     44:B1:76:F1:B5:CE

If the adapter cannot connect, it keeps retrying and the status shows why:

============================================  =======================================
``Reason:``                                   What to check
============================================  =======================================
SSID not found (out of range or 5 GHz only?)  spelling of the SSID, distance, 2.4 GHz
authentication failed (wrong password?)       password, WPA mode of the access point
association failed                            access point refuses the client
                                              (MAC filter, client limit)
connection lost (signal too weak?)            distance, antenna position
============================================  =======================================

Host name
---------

The adapter registers in the network under a host name. By default it is
``ar488-`` followed by the last six digits of its MAC address, for example
``ar488-f1b5ce`` – so several adapters can be told apart without any setup. Putting
a label with this name on the case is a good idea.

.. code-block:: text

   ++wifi host                show the current host name
   ++wifi host gpib-lab2      set a custom name
   ++wifi host auto           back to the default ar488-xxxxxx
   ++wifi connect             apply the new name now
   ++savecfg                  keep it

Allowed are up to 31 characters ``a-z``, ``0-9`` and ``-``, not starting or ending
with ``-``.

The name is used in two ways:

- **DHCP:** the router learns the name; whether it can be resolved via the router's
  DNS depends on the router.
- **mDNS:** the adapter answers to ``<host name>.local`` (for example
  ``ar488-f1b5ce.local``) and announces its console as a ``_telnet._tcp`` service.
  Linux (Avahi), macOS and Windows 10/11 resolve ``.local`` names out of the box.

.. note::

   mDNS only works **within one network segment**. If the PC and the adapter are in
   different subnets (for example a wired office network and a separate lab WiFi), the
   ``.local`` name cannot be resolved unless the router forwards mDNS (called *mDNS
   reflector*, *mDNS repeater* or *Multicast DNS* depending on the router). Otherwise
   use the IP address or a fixed DNS entry on the router.


Network Access
==============

Telnet console
--------------

The console is available on **TCP port 23**. It is a plain TCP stream (no telnet
option negotiation), so any raw TCP client works:

.. code-block:: bash

   nc ar488-f1b5ce.local 23          # or: telnet 192.168.5.186 23

Only **one network client** is accepted at a time. While a client is connected, any
further connection is closed at once, so a second program cannot interrupt a running
session or measurement. The new program has to wait until the first one disconnects
(it may simply retry). When the client disconnects, the adapter notices it within
about a quarter of a second and is free for the next one.

USB and the network console work at the same time. Every command is answered on the
console it came from: whichever console sent the last line gets the answers, and the
other one is told ``Moving serial console to TCP`` (or ``serial``). Do not let two
programs use USB and the network at the same time.

Speed of long answers
---------------------

Since firmware 0.49.16 the adapter collects the bytes it reads from the bus and sends
them to the console in blocks of up to 256 bytes. Before, every byte went out as a
TCP packet of its own (the console runs with *no delay*, so that short answers are not
held back). Over WiFi that cost about 4 ms per byte, about 270 bytes per second.

Measured with an R&S FSIQ 7 on the bus, over WiFi:

=====================================  ===========  ===========
Answer                                 0.49.15      0.49.16
=====================================  ===========  ===========
``TRAC? TRACE1`` (500 values, 9 kB)    33 s         0.27 s
``*IDN?`` (38 bytes)                   0.5 s        0.02 s
=====================================  ===========  ===========

Nothing changes for the program on the other side: the answer arrives in larger
pieces, but byte for byte the same, still followed by the EOT character if
``++eot_enable`` is on. Over USB the effect is smaller, because the USB port already
collected the bytes.

While it reads, the adapter looks for a new command line from the console only
after every 64 bytes (each look is a call into the network stack). A command sent to
end a long read with ``++auto 3`` therefore takes effect at the next 64-byte step, not
at once. For a program that leaves too long a timeout this hardly matters: the read
ends at EOI or at the end character as before.

NI-VISA and other VISA libraries
--------------------------------

The adapter is not a native VISA GPIB board (it does not appear as
``GPIB0::18::INSTR``). Use it as a **raw socket resource** instead:

.. code-block:: text

   TCPIP0::192.168.5.186::23::SOCKET
   TCPIP0::ar488-f1b5ce.local::23::SOCKET

In NI MAX: *Devices and Interfaces → Create New → VISA TCP/IP Resource → Manual Entry
of Raw Socket*, enter the address and port 23.

Two settings matter:

- **Enable the termination character** (``VI_ATTR_TERMCHAR_EN = true``, ``\n``). A
  socket has no EOI signal; without it, every read waits for the timeout.
- **Send the** ``++`` **commands yourself.** Select the instrument with ``++addr`` and
  let the adapter read automatically with ``++auto 2``. When several instruments are
  on the bus, send ``++addr`` before switching to another one.

Python example (PyVISA works with NI-VISA or, without any NI software, with
``pyvisa-py``):

.. code-block:: python

   import pyvisa

   rm = pyvisa.ResourceManager()          # use ResourceManager('@py') for pyvisa-py
   gpib = rm.open_resource(
       "TCPIP0::ar488-f1b5ce.local::23::SOCKET",
       read_termination="\n",
       write_termination="\n",
       timeout=5000,
   )
   gpib.write("++addr 18")    # instrument address
   gpib.write("++auto 2")     # read the answer after each query
   gpib.write("++eor 2")      # instrument ends its answers with LF
   print(gpib.query("*IDN?"))

The same works over USB with the resource ``ASRL/dev/ttyACM0::INSTR`` (Linux) or
``ASRLx::INSTR`` (Windows).


Command Overview
================

Commands marked **new** or **changed** differ from the original AR488 firmware.
See the :ref:`Command Reference` for the full description of each command.

Instrument communication
------------------------

======================  ==============================================================
``++addr [n]``          show/set the instrument address (1–30)
``++auto [0-3]``        automatic read: 0 off, 1 after every command, 2 after queries,
                        3 continuous
``++read [eoi|char]``   read the instrument's answer (until EOI / the given character)
``++read_tmo_ms [ms]``  read timeout, 1–32000 ms; larger values are rejected
                        (``Unrecognized command``)
``++eos [0-3]``         characters appended when sending to the instrument
``++eor [0-7]``         characters that end an answer from the instrument
``++eoi [0|1]``         assert EOI with the last byte sent
``++eot_enable [0|1]``  append ``eot_char`` to the output when EOI is detected
``++eot_char [n]``      character (decimal) for ``eot_enable``
======================  ==============================================================

Bus control
-----------

=====================  =================================================
``++ifc``              interface clear – take control of the bus
``++clr``              device clear for the current address
``++dcl``              device clear for all instruments
``++trg [addr …]``     trigger the current or the listed instruments
``++llo`` / ``++loc``  lock / unlock the instrument's front panel
``++ren [0|1]``        assert or release REN
``++spoll [addr]``     serial poll; ``++allspoll`` lists all status bytes
``++srq``              state of the SRQ line
``++srqauto [0|1]``    serial poll automatically when SRQ is asserted
``++findrqs``          find the instrument requesting service
``++findlstn``         list all instruments on the bus
``++ppoll``            parallel poll
``++tct addr``         pass control to another controller
=====================  =================================================

Adapter
-------

=====================  ========================================================
``++ver``              firmware version
``++mode [0|1]``       0 device, **1 controller**
``++savecfg``          save the configuration – **changed:** always confirms
``++default``          load factory defaults
``++verbose [0|1]``    human-readable messages and error texts
``++prompt [0|1]``     show a prompt
``++id …``, ``++idn``  interface name/serial number, answer to ``*IDN?`` itself
``++macro …``          stored command sequences; macro 0 runs at power-on
``++repeat …``         repeat a command
``++lon`` / ``++ton``  listen-only / talk-only mode
``++status [n]``       status byte for serial polls (device mode)
``++tmbus [µs]``       bus settling delay
``++xdiag mode byte``  set bus lines for 10 s (hardware diagnostics)
``++rst``              restart the adapter
=====================  ========================================================

WiFi
----

==============================  ===================================================
``++wifi`` / ``++wifi status``  **new:** connection state, IP, mDNS name, signal,
                                telnet client, last error
``++wifi scan``                 **changed:** list networks with signal and channel;
                                works before WiFi is set up
``++wifi ssid [name]``          show/set the network name (max. 31 characters)
``++wifi pass [password]``      **changed:** set the password (8–63 characters);
                                query only answers ``set``/``not set``
``++wifi host [name|auto]``     **new:** show/set the host name
``++wifi connect``              connect now with the current settings
``++ota [url]``                 **new:** without argument: running partition and
                                version; with an ``http://`` URL: update the
                                firmware over WiFi (see `Updating over WiFi`_)
==============================  ===================================================

.. note::

   ``++help`` is not included in the Rev5 build to save flash; this manual and the
   :ref:`Command Reference` replace it.


Troubleshooting
===============

**No answer on the USB console.**
   Check that no other program has the port open (only one program can use it at a
   time). If you typed something without pressing Enter in an earlier session, the
   first command may be answered with nothing; press Enter once and repeat.

**Every query takes about one second, or answers are followed by an empty line.**
   ``++eor`` does not match the instrument's terminator – see `Response termination`_.

**Error** ``timeout waiting for receiver attention [NDAC asserted]`` **(shown with** ``++verbose 1``\ **).**
   No instrument reacts on the bus: check the cable, that the instrument is switched
   on, and its GPIB address. A switched-off instrument on the cable can block the bus
   – disconnect it or switch it on.

**Error** ``timeout waiting for data accepted signal - [NDAC unasserted]``.
   An instrument reacts but does not accept data. Often another instrument on the
   bus is switched off or in a fault state.

**Nothing at the selected address, but** ``++findlstn`` **lists another one.**
   The instrument is set to a different address than expected – check its GPIB/IEC
   bus menu.

**WiFi does not connect.**
   ``++wifi status`` shows the reason. SSID and password are case sensitive; only
   2.4 GHz networks work. ``++wifi pass`` answers ``not set`` if no password is
   stored – enter it again. After changing settings, ``++wifi connect`` applies them and
   ``++savecfg`` keeps them.

**The** ``.local`` **name does not resolve.**
   PC and adapter are probably in different subnets – see the note in `Host name`_.

**The adapter does not show up on USB at all.**
   Unplug and replug it. If it still does not appear, it may be stuck in firmware
   update mode or have no valid firmware – reflash it as described below.


.. _Updating the Firmware:

Updating the Firmware
=====================

There are two ways to install new firmware: over WiFi with ``++ota`` (no cable, the
adapter can stay in the rack), or over USB with ``esptool``. Both keep the saved
settings, including the WiFi data. ``++ver`` shows the installed version afterwards.

Build the firmware first:

.. code-block:: bash

   pio run -e AR488-ESP32-Rev5

The images are then in ``.pio/build/AR488-ESP32-Rev5/``.

Updating over WiFi
------------------

The flash memory holds two firmware slots (``app0`` and ``app1``). ``++ota`` loads a
new image into the slot that is not running and restarts from it. The adapter
fetches the image itself by HTTP, so any web server on a PC it can reach will do; no
port on the adapter is opened for this, and it also works across subnets.

1. Serve the build directory, for example with Python on the PC (any free port):

   .. code-block:: bash

      cd .pio/build/AR488-ESP32-Rev5
      python3 -m http.server 8016

2. Give the adapter the URL of ``firmware.bin`` (on USB or the telnet console)::

      ++ota http://192.168.5.61:8016/firmware.bin
      OTA: fetching http://192.168.5.61:8016/firmware.bin
      OTA: done, restarting

   Loading takes about 5 seconds. The adapter then restarts with the new firmware
   and reconnects to WiFi; a telnet session is closed by the restart and has to be
   opened again.

3. Check the result::

      ++ota
      running from app1, AR488 GPIB controller, ver. 0.49.16, 22/11/2026

Only ``http://`` URLs are accepted (no ``https://``), and the adapter must be
connected to WiFi. Give it ``firmware.bin`` only – not ``bootloader.bin``,
``partitions.bin`` or a merged image.

**If anything goes wrong, nothing is changed.** When the server cannot be reached,
the file is missing or the download breaks off, the adapter answers
``OTA failed: <reason>`` and keeps running the old firmware; the new slot is only
activated after the complete image was written and checked. Fix the cause and repeat
the command. The previous firmware stays in the other slot until the next update.

.. note::

   The image is not authenticated: anyone who can send commands to the adapter can
   load any firmware into it. Only use ``++ota`` in a network you trust.

Updating over USB
-----------------

The adapter's native USB port is also its programming port. Opening it at 1200 baud
(or holding **BOOT** while pressing **RESET**) switches the ESP32-S2 into its ROM
boot loader. It then reappears as a different USB device: ``303a:0002``, reported as
``ESP32-S2`` with serial number ``0``, on Linux
``/dev/serial/by-id/usb-Espressif_ESP32-S2_0-if00``.

``pio run -t upload`` does **not** work with this board: the adapter drops into the
ROM boot loader and the upload fails there. Flash with esptool directly instead and
tell it not to reset the chip (``--before no_reset``):

1. Close every program that has the adapter's port open.

2. Switch the adapter into the boot loader, either by opening the port at 1200 baud

   .. code-block:: bash

      stty -F /dev/serial/by-id/usb-Espressif_Systems_AR488_GPIB-USB_F1B5CE-if00 1200

   or by hand: hold **BOOT**, press and release **RESET**, release **BOOT**. Check
   that ``/dev/serial/by-id/usb-Espressif_ESP32-S2_0-if00`` exists.

3. Write the four images (run from the project directory):

   .. code-block:: bash

      B=.pio/build/AR488-ESP32-Rev5
      pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s2 \
          --port /dev/serial/by-id/usb-Espressif_ESP32-S2_0-if00 \
          --before no_reset write_flash \
          0x1000  $B/bootloader.bin \
          0x8000  $B/partitions.bin \
          0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
          0x10000 $B/firmware.bin

   A separately installed esptool (``pip install esptool``, command ``esptool``)
   works the same way.

4. esptool resets the chip after writing (``--after hard_reset`` is its default), and
   the adapter starts the new firmware on its own; it appears again under its own
   ``/dev/serial/by-id/`` name after a few seconds. This is what happened on all five
   adapters updated on 2026-11-23. If it does not come back, press **RESET** (or unplug
   and replug it).

The settings in flash (NVS), including the WiFi data, are not touched by this.
Writing ``boot_app0.bin`` makes the adapter start from ``app0`` again, even if it
was running from ``app1`` after an ``++ota`` update.

.. important::

   **Put only one adapter at a time into the boot loader.** In the boot loader all
   adapters report the same name and serial number ``0``, so there is no way to tell
   them apart, and the ``by-id`` link points to just one of them. If you prefer to
   address the adapter by its USB socket, use its ``/dev/serial/by-path/`` link
   (``ls -l /dev/serial/by-path/`` lists them); that link stays the same in the boot
   loader.

If the adapter does not respond at all or does not show up on USB, start the boot
loader by hand (BOOT + RESET) and flash it as above.
