# Replay
Save the last few seconds of your computer's audio, like an audio version of an instant replay.

Replay lets you record the last 30 seconds (duration is adjustable) of system audio whenever needed. You can run Replay in the background during virtual school lessons and if you miss a key sentence in the lesson you can hit save. This negates the need to record entire lessons in case you want to revisit them.

Replay is written entirely in C and uses relatively little cpu, which makes it suitable to run in background at all times. Memory usage depends on the duration of audio you set it to record.

Replay was created as a lightweight alternative to recording virtual school lessons. My laptop couldn't handle recording video while attending a lesson, and I also didn't want to waste disk space with large recordings.

## Usage

It is recommended to set replay to run on startup (by adding it to ~/.xinitrc or ~/.xprofile).

The recording can be saved by sending a SIGUSR1 to the running replay thread. This can be done by the following command:
```
kill -s SIGUSR1 `pgrep replay`
```
You may wish to bind this command to a keypress for ease of use.

## Installation

You will need to compile Replay manually, but it is very straightforward. See the Build subheading below. 

## Options
```
[--list-devices]      lists the audio input and output devices found 
[--device id]         select the device replay uses (use --list-devicse to find the id)
[--duration seconds]  set the duration of audio to save when requested (Default 30)
[--help]              see this help message
```
## Build

First install the dependancies:
- cmake
- [libsoundio](https://github.com/andrewrk/libsoundio) (grab the latest release [here](https://github.com/andrewrk/libsoundio/releases))
- libsndfile

cmake and libsndfile are likely available in distribution repositories, but you will need to build libsoundio from source.

Then clone this repo run the following commands in that directory:

```
mkdir build
cd build
cmake ..
make
sudo make install
```

To uninstall, simply run the following command from that same build directory used above:
```
sudo make uninstall
```
