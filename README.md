# Resample EEG and use it as audio

This package can be used to upsample an EEG data stream (for example at 250 Hz) to a standard audio sampling rate. The recommended output sample rate for Ableton Live is 44100 or 48000 Hz.

For macOS you can use [BlackHole](https://github.com/ExistentialAudio/BlackHole) (recommended) or [SoundFlower](https://github.com/mattingalls/Soundflower) (outdated) to create virtual audio devices.

For Windows you can use [VB-Audio Cable](https://vb-audio.com/Cable/index.htm) to create virtual audio devices.

## resampleaudio

This application takes an input audio stream - for example the 8000 Hz stream from the EAVI board - and resamples/upsamples and streams it to another (virtual) output audio device.

This application makes use of [PortAudio](http://www.portaudio.com) and [Secret Rabbit Code (aka libsamplerate)](http://libsndfile.github.io/libsamplerate/).

## resamplelsl

This application takes an input LSL stream - for example from the Unicorn EEG system - and resamples/upsamples and streams it to another (virtual) output audio device.

This application makes use of [PortAudio](http://www.portaudio.com), [libsamplerate (aka Secret Rabbit Code )](http://libsndfile.github.io/libsamplerate/) and [liblsl (aka LabStreamingLayer)](https://labstreaminglayer.readthedocs.io/projects/liblsl/).

# External dependencies

- <http://www.portaudio.com> and <http://libsndfile.github.io/libsamplerate> for both applications
- <https://labstreaminglayer.readthedocs.io> for `resamplelsl`

You can install these with your platform-specific package manager (homebrew, apt, yum), after which they will end up in `/usr/local/lib` and `/usr/local/include`. You can also install them manually in the `external` directory. In that case the directory layout should be

```
external/
├── portaudio
│   ├── include
│   └── lib
├── samplerate
│   ├── include
│   └── lib
└── lsl
    ├── include
    └── lib
```


# Copyrights

Copyright (C) 2022, Robert Oostenveld

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
