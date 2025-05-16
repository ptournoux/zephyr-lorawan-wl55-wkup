This sample project illustrate how to use a nucleo_wl55jc to monitor an event on GPIO and send alert using LoRaWAN.

It features :
* low average consumption
  * poweroff (standby mode) and wakeup on a GPIO event
  * low power (STOP2) between events
* lorawan OTAA devnonce storage on flash


# Getting started

## Compile

If you've followed the [install procedure of the Zephyr Getting started](https://docs.zephyrproject.org/latest/develop/getting_started/index.html), you can add the project to your workspace and build it as follow :

```bash
cd zephyrproject/zephyr
git clone git@github.com:ptournoux/zephyr-lorawan-wl55-wkup.git
west build -p  -b nucleo_wl55jc zephyr-lorawan-wl55-wkup
```

## Pins

### Wake up pin

In the overlay file, the wake up pin is associated to GPIOA pin 0 which is also wired to user button 0 and PIN 0 of CN10.

Use either the button or a normaly closed reed switch.

The NC reed switch has been connected to :
* PIN 0 of CN10.
* GND

We higly recommend adding a resistance.

### Powering the nucleo board

Check the [documentation](./docs/um2592-stm32wl-nucleo64-board-mb1389-stmicroelectronics.pdf) section 6.4.

In our case, we chose to connect a 4.5V (3 AAA) battery pack to the STD_ALONE_5V pin. The LD39050PU33R 3.3v  regulator (which accepts up to 5v) seemed to be happy with it.

## Usage

When powered, the device check the status of GPIOA0. 

* If voltage is VDD on GPIOA0 (gpio val = 0) then it goes to standby mode (`sys_poweroff()`).
* Otherwise, it joins (using the last stored devnonce+1) and send a message periodically (e.G. every 2min).

The payload of the lorawan message is 1 byte containing either 0 if the reed switch is not triggered (i.e. 0 == (val = gpio_pin_get_dt(&wkup));) or 1 otherwise.


If the reedswitch is re setted (magnetic field is applied to the switch), then device go back to standby mode (`sys_poweroff()`).




 


## Logs

The application log debug information on the UART.


```bash
picocom -b 115200 /dev/ttyACM0 # exact path depends on your OS, driver and other devices connected
```

You should see something like :

```
*** Booting Zephyr OS build v4.1.0-2053-g00cc4441aa23 ***
[00:00:00.053,000] <inf> lorawan_class_a: Wakeup pin value: 1
[00:00:00.053,000] <inf> lorawan_class_a: Wakeup pin is high, continuing...
[00:00:00.118,000] <inf> fs_nvs: 8 Sectors of 2048 bytes
[00:00:00.118,000] <inf> fs_nvs: alloc wra: 2, 7c0
[00:00:00.118,000] <inf> fs_nvs: data wra: 2, 448
SETTINGS: Loaded dev_nonce: 27
[00:00:00.133,000] <err> settings: set-value failure. key: lorawan/nvm/ClassB error(-2)
[00:00:00.134,000] <err> settings: set-value failure. key: lorawan/nvm/RegionGroup2 error(-2)
[00:00:00.135,000] <err> settings: set-value failure. key: lorawan/nvm/SecureElement error(-2)
[00:00:00.136,000] <err> settings: set-value failure. key: lorawan/nvm/MacGroup2 error(-2)
[00:00:00.136,000] <err> settings: set-value failure. key: lorawan/nvm/MacGroup1 error(-2)
[00:00:00.137,000] <err> settings: set-value failure. key: lorawan/nvm/Crypto error(-2)
SETTINGS: Initial dev_nonce: 27

```



# Consumption profile

Power consumption measurements are as follows:

**Measured on JP1 (STM32WL MCU consumption):**

*   **Standby Mode:** 400 nA (with GPIO active)
*   **Idle Mode:** 160 µA (Note: This can be optimized, current draw is mainly due to the GPIO without a pull-up/pull-down resistor)
*   **Run Mode:** 3 mA (at default CPU speed and voltage)
*   **LoRaWAN Tx:** ~90 mA (including LED activity)

**Measured on the battery pack (4.5V, 3xAAA) connected to STDALONE_5V pin (JP6 disconnected, no 5V power to the ST-LINK):**

*   **Standby Mode:** 19 µA (Likely includes consumption from the 5V voltage regulator)
*   **Idle Mode:** 187 µA

## Expected autonomy

Based on a 4.5V power source (3xAAA alkaline batteries, 1200mAh capacity) and a standby current of 19µA, the estimated operational lifetime is:

(1200 mAh / 0.019 mA) / 24 hours/day / 365 days/year ≈ 7.2 years

*Note: This calculation neglects battery self-discharge rates and consumption during active/transmit periods.*


# Known Issues and Potential Improvements

The following areas have been identified for further development and optimization:

*   **Session Key Persistence:** Implement storage of LoRaWAN session keys to avoid re-joining after each device boot.
*   **GPIO Current Optimization:** Add an appropriate pull-up or pull-down resistor to the reed switch GPIO to minimize current draw in idle states.
*   **GNSS:** Add positionning system so that it is easier to retrieve in the wild.
*   **NVS Error Handling:** Implement robust checking and handling for Non-Volatile Storage (NVS) errors.
*   **Code Refinement:** General code cleanup and refactoring for improved readability and maintainability.