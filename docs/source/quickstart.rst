.. _quickstart:

Quickstart
==========

.. note::

   For the AR488-ESP32 Rev5 board, the :ref:`User Manual` is the better starting
   point. This chapter shows the general use with any supported board.

The idea of the Prologix serial communication protocol is to use a special
syntax for control commands of the AR488-ESP32 interface. All other
communication stream sent to the AR488-ESP32 will be forwarded (almost) as is
(see below) to the adressed GPIB device, if any.

Any received data stream from the adressed device will be similarly sent the
computer host's serial interface.

The complete list of special `++` commands is described in the next
:ref:`Command Reference` section.

Note that the AR488-ESP32 will not echo characters received from the serial
line. So when using the device in an interactive session, make sure to enable
ECHO on your terminal program.

Using the platformio monitor tool:

.. code-block::

   AR488-ESP32$ pio device monitor -b 115200 --echo
   --- Terminal on /dev/ttyUSB0 | 115200 8-N-1
   --- Available filters and text transformations: colorize, debug, default, direct, hexlify, log2file, nocontrol, printable, send_on_enter, time
   --- More details at https://bit.ly/pio-monitor-filters
   --- Quit: Ctrl+C | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H
   > ++ver
   AR488-ESP32 GPIB controller, ver. 0.0.1
   > ++prompt
   Show prompt: ON

In the example above one can see the prompt has been activated, which is useful
for an interactive session (but you probably do not want that for automated
control).

WiFi is set up with the ``++wifi`` commands, see :ref:`WiFi setup`.

Now let's see available devices on the bus (note that older devices may not be
detected):

.. code-block::

   > ++auto 2
   > ++findlstn
   3 4 6 8 10 12 17 24 26
   > ++addr 10
   > *IDN?
   HEWLETT-PACKARD,34970A,0,12-1-2

   > ++addr 26
   > *IDN?
   HP 8904A Opts 02/01/03/05/  /  /  /  /  Firmware Revision 00790A Serial No 06958
   > ++addr 4
   > *IDN?
   Hewlett-Packard, ESG-D3000B, GB38450564, B.03.86

   > ++addr 8
   > *IDN?
   ADVANTEST,R3465,0,K03
   > ++addr 12
   > *IDN?
   GIGA-TRONICS,8541C,1835243,4.09
   > ^C

You may prefer to manually poll individual devices as well:

.. code-block::

   > ++verbose 1
   Verbose: ON
   > ++addr 4
   Set device primary address to: 4
   > ++spoll
   Got 1 devices to spoll
   Polling 4
   4
   Serial poll completed.
   > *IDN?
   > ++read
   Hewlett-Packard, ESG-D3000B, GB38450564, B.03.86
   > ++verbose 0
   Verbose: OFF
   > ++addr 6
   ++spoll
   0
   > *IDN?
   > ++read
   Agilent Technologies,E3648A,0,1.7-5.0-1.0
   > ++addr 8
   > ++spoll
   0
   > *IDN?
   > ++read
   ADVANTEST,R3465,0,K03
   > ^C
   AR488-ESP32$

You may use Python to write a simple tool to poll the whole bus:

.. code-block:: python

   from serial import Serial
   import time
   cnx = Serial(port='/dev/ttyUSB0', baudrate=115200,
				timeout=0.1)

   cnx.readline()  # just in case
   for i in range(1, 31):
       cnx.write(f"++addr {i}\r\n".encode())
       cnx.readline()
       cnx.write("++spoll\r\n".encode())
       for j in range(10):
           time.sleep(0.05)
           if cnx.readline().decode().strip():
               print("Device at", i, "spoll=", r)
               break

Which would be like:

.. code-block:: bash

   $ python3 detect.py
   Device at 4 spoll= 4
   Device at 6 spoll= 0
   Device at 8 spoll= 0
   Device at 12 spoll= 0
   Device at 24 spoll= 0
   Device at 26 spoll= 0

The ``++auto`` feature is also supported; it automatically requests talk after an
input command, with several possible values:

- ``++auto 0``: disable auto mode completely.
- ``++auto 1``: "Prologix" mode; note that some devices do not like it too much). 
- ``++auto 2``: "on query" mode; it will only request talk if the command ends with the ``?`` character.
- ``++auto 3``: "continuous" mode; constantly read.


.. code-block::

   AR488-ESP32$ pio device monitor -b 115200 --echo
   ++prompt 1
   Show prompt: ON
   > ++auto 2
   > ++addr 4
   > *IDN?
   Hewlett-Packard, ESG-D3000B, GB38450564, B.03.86

   > ++addr 26
   > *IDN?
   HP 8904A Opts 02/01/03/05/  /  /  /  /  Firmware Revision 00790A Serial No 06958
   > ^C
   AR488-ESP32$
