# NodeMCU v2 controller: pinout and reference circuit

Matches `include/config.h`: two 74HC595 registers, 16 active-low relay channels, four active-high PWM channels, AHT20 and SSD1306 128x32 I2C display.

Your hardware is a 12 V, 16-channel relay module and PWM-controlled LED strips. The logic connections are fixed by the firmware. The relay interface below assumes low-trigger inputs, input current no more than 5 mA, and input voltage no more than 12 V. Confirm the relay board's trigger polarity and input specifications. The PWM circuit assumes constant-voltage DC LED strips; their voltage/current is still unspecified. This is a reference circuit, not a tested PCB design.

## NodeMCU connections

| Board label | GPIO | Connection |
|---|---:|---|
| D0 | 16 | Onboard LED function; verify LED pin on your board revision |
| D1 | 5 | AHT20 SCL and OLED SCL |
| D2 | 4 | AHT20 SDA and OLED SDA |
| D3 | 0 | U1 and U2 pin 12: latch ST_CP/RCLK |
| D4 | 2 | U1 and U2 pin 13: active-low output enable OE |
| D5 | 14 | U1 and U2 pin 11: clock SH_CP/SRCLK |
| D6 | 12 | PWM light 1 driver |
| D7 | 13 | U1 pin 14: serial data DS/SER |
| D8 | 15 | PWM light 3 driver |
| TX | 1 | PWM light 2 driver; boot UART pulses are possible |
| RX | 3 | PWM light 4 driver; isolate onboard USB-UART TX first |
| 3V3 | — | Both 74HC595 supplies and compatible AHT20/OLED modules |
| G/GND | — | Common low-voltage ground |

Use USB 5 V to power the NodeMCU for the initial setup. A separate regulated supply powers the relay board and LED strips. Do not feed relay coils or LED strips from NodeMCU 3V3. Do not connect 5 V to any ESP GPIO. External board supply wiring depends on the NodeMCU revision; verify its schematic before using VIN or combining external 5 V with USB power.

## Two 74HC595s: complete wiring

U1 is the first register, directly receiving NodeMCU data. U2 is the second.

| IC pin | Name | U1 connection | U2 connection |
|---:|---|---|---|
| 16 | VCC | 3.3 V | 3.3 V |
| 8 | GND | GND | GND |
| 14 | DS/SER | D7 / GPIO13 | U1 pin 9 |
| 11 | SH_CP/SRCLK | D5 / GPIO14 | D5 / GPIO14 |
| 12 | ST_CP/RCLK | D3 / GPIO0 | D3 / GPIO0 |
| 13 | OE | D4 / GPIO2 | D4 / GPIO2 |
| 10 | MR/SRCLR | 3.3 V | 3.3 V |
| 9 | Q7S/QH prime | U2 pin 14 | Unconnected |
| 15 | Q0/QA | Relay interface 1 | Relay interface 9 |
| 1 | Q1/QB | Relay interface 2 | Relay interface 10 |
| 2 | Q2/QC | Relay interface 3 | Relay interface 11 |
| 3 | Q3/QD | Relay interface 4 | Relay interface 12 |
| 4 | Q4/QE | Relay interface 5 | Relay interface 13 |
| 5 | Q5/QF | Relay interface 6 | Relay interface 14 |
| 6 | Q6/QG | Relay interface 7 | Relay interface 15 |
| 7 | Q7/QH | Relay interface 8 | Relay interface 16 |

These numbers apply to the standard 16-pin package, viewed from the top. Pin 9 is the serial cascade output, not relay output 8. Verify the notch/dot before inserting either IC.

Required passives:

- 100 nF ceramic directly between pins 16 and 8 on each IC.
- 10 kOhm from the shared OE/D4 net to 3.3 V. Do not ground OE.
- 10 kOhm from the shared latch/D3 net to 3.3 V.
- 10 kOhm from clock/D5 to GND.
- Preserve GPIO15/D8's boot pull-down; a PWM gate pull-down can provide additional bias.
- Suggested 10 uF local bulk capacitor across the 3.3 V logic rail.

```
NodeMCU D7 ----> U1 DS(14)      U1 Q7S(9) ----> U2 DS(14)
NodeMCU D5 ----> U1 CLK(11) ------------------> U2 CLK(11)
NodeMCU D3 ----> U1 LAT(12) ------------------> U2 LAT(12)
NodeMCU D4 ----> U1 OE (13) ------------------> U2 OE (13)
                 |                              |
              Q0..Q7                         Q0..Q7
                 |                              |
           interfaces 1..8               interfaces 9..16
                 |                              |
           relay IN1..IN8                relay IN9..IN16
```

GPIO0 and GPIO2 must be HIGH, and GPIO15 LOW, when booting normally. OE HIGH makes register outputs high impedance. Firmware loads all HIGH outputs (relays OFF) before enabling them. External circuitry must keep the relays off while those outputs float. GPIO2 may emit UART data during serial flashing: disconnect relay power while programming; the pull-up alone does not prevent pulses during that mode.

## Relay module interface: repeat 16 times

Use this two-transistor interface when the module input is pulled up to a voltage above 3.3 V, or when direct 3.3 V compatibility is unconfirmed. It keeps the firmware's active-low polarity and prevents the module input pull-up from reaching the 74HC595 output.

```
                         +3.3 V
                            |
                       E  QP (2N3906, PNP)
595 Qn ---- 10 kOhm ---- B
                       C    |
                       |    +---- 100 kOhm ---- +3.3 V
                       |
                    4.7 kOhm
                       |
                       +---- B  QN (2N3904, NPN)
                       |          C ---- relay module INn
                    100 kOhm      E
                       |          |
                      GND        GND
```

The 100 kOhm at QP connects **base to emitter**. The 100 kOhm at QN connects **base to ground**. Check each transistor's actual package pinout; B/C/E order varies by manufacturer and package.

At module INn, add a 10 kOhm pull-up to the module's specified input logic supply if the module lacks a reliable off-state pull-up. This is the module logic rail, which is not necessarily its coil/JD-VCC rail. The interface is intended for inputs requiring at most 5 mA sinking, including the added pull-up current. Confirm the module input thresholds and off-state voltage. It does not power a bare relay coil.

| Register output | QP | QN | Module IN | Relay |
|---|---|---|---|---|
| LOW | On | On | LOW | On |
| HIGH | Off | Off | Pulled HIGH | Off |
| High impedance, OE HIGH | Off via base pull-up | Off via base pull-down | Pulled HIGH | Off |

A known compatible 3.3 V active-low module may allow direct Qn-to-INn wiring with off-state pull-ups to 3.3 V instead. Only use that simpler option after confirming voltage compatibility, current per output and total IC current. Never allow a 5 V input pull-up onto a 3.3 V 74HC595 output.

Power your relay module's coil supply from regulated 12 V. For a board with VCC/JD-VCC and a jumper, follow that board's schematic: its logic supply and coil supply arrangement cannot be inferred from the labels alone. Do not assume the module's logic VCC also takes 12 V. The interface above uses a common low-voltage ground and does not provide galvanic isolation. Use modules with built-in coil drivers and flyback protection.

## AHT20 and OLED

| Signal | AHT20 module | SSD1306 I2C module |
|---|---|---|
| Supply | 3.3 V, if supported | 3.3 V, if supported |
| Ground | GND | GND |
| SDA | D2 / GPIO4 | D2 / GPIO4 |
| SCL | D1 / GPIO5 | D1 / GPIO5 |
| Address | 0x38 | 0x3C |

Use a 128x32 SSD1306 I2C module to match this firmware. Check the module's printed labels; connector order varies. If absent on the modules, add one 4.7 kOhm pull-up from SDA to 3.3 V and one from SCL to 3.3 V. Existing module pull-ups are parallel, so do not automatically add more. All bus pull-ups must go to 3.3 V.

## PWM power stage: repeat four times for DC LED strips

The firmware generates 1 kHz PWM. This example is for constant-voltage LED strips with built-in current limiting, not bare high-power LEDs or AC lamps.

```
LED supply + (e.g. 12 V or 24 V) ---- fuse ---- LED strip +
                                                    LED strip -
                                                         |
                                                         D
ESP PWM GPIO ---- 220 Ohm ----+---- G   N-channel MOSFET
                             |                           S
                          10 kOhm                        |
                             |                           |
                            GND ------------------------ GND
                                                      LED supply -
```

Choose a MOSFET whose on-resistance is specified at 2.5 V or 3.3 V gate drive, and whose voltage, current, thermal limits and safe operating area suit the strip. Gate threshold voltage alone does not establish suitability. Use an appropriately rated fuse, wire and connector per branch. MOSFET package pinout and heatsinking depend on the selected part. Dimmer modules instead require their own documented signal interface.

| Light | GPIO | NodeMCU |
|---:|---:|---|
| 1 | 12 | D6 |
| 2 | 1 | TX |
| 3 | 15 | D8 |
| 4 | 3 | RX |

GPIO1 emits ROM boot messages, so light 2 can flash even with a gate pull-down. If it must remain off during boot, use a hardware output gate with a verified enable sequence; that gate is additional circuitry, not implemented by this drawing.

GPIO3 is commonly connected to the onboard USB-UART transmitter. Removing the USB cable may not isolate it because the adapter may still be powered by the board. Isolate that transmitter connection with a verified board modification/jumper before GPIO3 drives a PWM gate. Restore it for serial flashing. Without that modification, leave PWM light 4 disconnected. The diagnostic firmware reserves both GPIO1 and GPIO3 and disables lights 2 and 4.

## Power and contacts

```
USB 5 V ------------ NodeMCU USB
NodeMCU 3V3 -------- 74HC595 U1/U2 + compatible I2C modules
12 V relay supply + - relay board coil supply
LED supply + ------- fused LED branches

NodeMCU GND -------- logic GND -------- relay module GND
                              +-------- LED supply negative
```

Size supplies from actual ratings: relay supply current must exceed the sum of all simultaneously energized coil currents plus board electronics, with margin. For example, 16 coils at 70 mA require 1.12 A before margin. Route coil/LED return currents directly back to their supplies, keeping those currents out of sensor/ESP ground wiring. Locate bulk decoupling at the relay board per its manufacturer recommendations.

For an initially OFF, low-voltage DC test load:

```
Load supply + ---- appropriately rated fuse ---- relay COM
relay NO -------------------------------------- load +
load - ---------------------------------------- load supply -
relay NC -------------------------------------- unused
```

Relay contacts are separate from the control circuit. A separately supplied contact load does not need its negative connected to ESP ground. Use contact protection appropriate to inductive loads. This guide covers low-voltage wiring; it does not specify a mains installation or its enclosure/clearances.

## Parts and checks

- NodeMCU v2; two 74HC595s; compatible AHT20 and 128x32 SSD1306 modules.
- One 16-channel or two 8-channel active-low relay modules, with coil drivers and suppression.
- For the transistor interface: 16 PNPs, 16 NPNs, 16 x 10 kOhm base resistors, 16 x 4.7 kOhm base resistors, and 32 x 100 kOhm base bias resistors; input pull-ups as required.
- Four suitable MOSFETs, four 220 Ohm gate resistors and four 10 kOhm gate pull-downs for DC strips.
- Logic pull-ups/pull-downs, decoupling, rated supplies, fuses and connectors as described above.

Check supply rails and chip orientation before power-up. Initially disconnect relay coil power and load supplies. Verify boot straps and off-state module inputs, then test one relay at a time with a low-voltage load. Verify cold start, reset, brownout and programming behavior before connecting installed loads. See `COMMISSIONING.md` for the project's hardware acceptance procedure.

Sources: firmware `include/config.h`, repository `README.md`, [TI SN74HC595 datasheet](https://www.ti.com/lit/ds/symlink/sn74hc595.pdf), and [Espressif boot-mode documentation](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html).

## Recommended revision to free TX/RX (not yet implemented)

Changing only pin numbers cannot preserve all current functions while freeing TX and RX. D0-D8 provide nine pins: two are needed for I2C, four for the 74HC595 interface, four for PWM, and one for the independent LED function, totaling eleven. Removing the independent LED function still requires ten. A0 is an analog input; GPIO6-GPIO11 belong to the flash interface and are unsuitable substitutes for a reliable NodeMCU design.

Recommended hardware addition: a PCA9685 I2C PWM module, powered at 3.3 V, with channels 0-3 connected to the four MOSFET gate circuits. The module generates control signals only; it does not supply LED-strip power. Keep strip supplies separate. It can share SDA/SCL with the AHT20 and OLED. Its usual individual address 0x40 does not conflict with 0x38 or 0x3C; verify the module's address straps and configured group addresses.

Proposed mapping:

| Function | Revised connection |
|---|---|
| I2C SDA/SCL, including PCA9685 | D2/GPIO4 and D1/GPIO5 |
| 74HC595 clock/data | D5/GPIO14 and D7/GPIO13 |
| 74HC595 latch | D3/GPIO0, keep 10 kOhm pull-up |
| Shared enable: both 74HC595 OE and PCA9685 OE | D6/GPIO12, with 10 kOhm pull-up to 3.3 V |
| Independent onboard LED function | D0/GPIO16, subject to board revision |
| PWM lights 1-4 | PCA9685 channels 0-3, then four MOSFET stages |
| TX/GPIO1 and RX/GPIO3 | Reserved for USB serial |
| D4/GPIO2 and D8/GPIO15 | No external controller loads; preserve normal boot bias |

The revised shared OE uses a pin outside the boot-strapping/UART functions. Both chips use active-low OE. Firmware must configure PCA9685 disabled outputs LOW (not HIGH), configure non-inverted totem-pole PWM for the MOSFET gates, initialize every channel to full OFF, and latch all relays OFF before lowering OE. Configure approximately 1 kHz PWM and retain gate pull-downs. Verify the module exposes OE and remove any conflicting onboard OE pull-down; a pull-up must keep it disabled before firmware runs.

This revision needs a PCA9685 driver replacing every direct PWM write, updated startup/OTA/all-off handling, and a defined policy for I2C failures. PCA9685 PWM persists independently of the ESP, so missing communication must not silently be treated as an OFF command having succeeded. Sharing OE means disabling it also disables all relays; account for that in the fault policy. Test startup, reset, programming, brownout and OTA with hardware before use. The current firmware and the earlier pin tables remain unchanged until this hardware revision is implemented.

References: [ESP8266 Arduino GPIO/PWM reference](https://arduino-esp8266.readthedocs.io/en/stable/reference.html), [Espressif boot-mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html), and [NXP PCA9685 datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf).
