# MAX98357A + I2S Microphone Test (ESP32-S3)

This is a temporary hardware test firmware for the Ravatronix ESP32-S3-N16R8 board.

## What it does

- Captures audio from an INMP441-compatible I2S microphone on I2S0.
- Plays a quiet 440 Hz test tone through a MAX98357A on I2S1.
- Prints a microphone RMS level periodically to the serial monitor.
- It does **not** connect microphone audio to the speaker, so it avoids an acoustic feedback loop.

## Current GPIO assignment

These pins match the wiring plan discussed for this test:

| Signal | ESP32-S3 GPIO |
|---|---:|
| Microphone SCK / BCLK | 38 |
| Microphone WS / LRCLK | 39 |
| Microphone SD / DOUT | 40 |
| Microphone L/R | GND (left channel) |
| MAX98357A BCLK | 1 |
| MAX98357A LRC / WS | 2 |
| MAX98357A DIN | 21 |

Connect microphone VDD to 3V3 and microphone GND to board GND. Connect MAX98357A VIN to the board's 5V supply and its GND to the same common ground. Connect the speaker only to the amplifier's SPK+ and SPK- terminals; neither speaker terminal goes to GND.

The microphone pinout is assumed to be INMP441-compatible. If the microphone IC is a different model, its I2S data format or channel selection may differ.

## Safety and test notes

- Start with low volume. The generated tone uses a deliberately low digital amplitude.
- Do not connect motors, servos, or the cellular modem during this first audio test.
- USB power may not be sufficient for loud speaker output. If the board resets or the sound cuts out, stop the test and check the power arrangement before trying again.
- This is a test application, not the complete Ravatronix firmware. Flashing it replaces the currently running application; the camera and robot-control features are not included in this build.
- The microphone RMS value is a relative activity indicator, not a calibrated sound-level measurement.

## Build

GitHub Actions builds the ESP-IDF project for ESP32-S3. Open the repository's **Actions** tab, select **Build ESP32-S3 audio test**, and run the workflow if it has not started automatically.

After a successful run, open the workflow run and download the artifact named `MAX98357A-Audio-Test-ESP32-S3`. It contains `merged-binary.bin`.

The build uses ESP-IDF v5.3.2 and the project defaults request QIO flash, 16 MB flash, and octal PSRAM. Verify the actual board configuration before flashing. The merged image is intended for this project's ESP-IDF layout; do not combine it with offsets or binaries from another firmware.
