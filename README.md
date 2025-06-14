# PicoVision Bluetooth MP3 Player

Bluetooth MP3 player for PicoVision, complete with an FFT display of the music.

Load up an SD card with some MP3 files, put your bluetooth speaker in pairing mode, and away you go!  MP3s must be in the root directory of the SD card, and are played in lexicographic order.

If it doesn't work first time, try resetting a couple of times - getting the bluetooth connection to come up properly can be a bit flaky.

## Credits

This project pulls together the Bluetooth audio source example, the [SDIO SD card library](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico) by carlk3 and an [MP3 wrapper library](https://github.com/ikjordan/picomp3lib/) from ikjordan, which is based on an Adafruit wrapper of the RealNetworks helix mp3 library.
