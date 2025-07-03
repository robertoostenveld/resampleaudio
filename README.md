# Use EEG as audio, or use audio as EEG

This package provides three command-line applications that provide a bridge between software that supports audio input and outputs, such as [Ableton Live](https://www.ableton.com), [Audacity](https://www.audacityteam.org) and [TouchDesigner](https://derivative.ca), and EEG acquisition systems and real-time EEG analysis software.

Audio contains high frequencies and is typically sampled at rates of 22050, 44100 or 48000 Hz, whereas EEG contains lower frequencies (up to a few hundred Hz) and is often sampled at rates of 250 or 500 Hz. The applications provided here can be used to upsample an EEG data stream to a standard audio sampling rate, or to downsample an audio data stream to a lower rate that is more common for EEG.

To receive input from an EEG system, or to stream signals to software designed for real-time EEG processing, these applications support the [Lab Streaming Layer](https://labstreaminglayer.org) (LSL) interface. Using LSL to receive the EEG data, the signal can be upsampled and streamed to a virtual audio interface to be picked up by audio software. You can also downsample the (processed) audio again, and send it back to an LSL stream.

For macOS you can use [BlackHole](https://github.com/ExistentialAudio/BlackHole) (recommended) or [SoundFlower](https://github.com/mattingalls/Soundflower) (outdated) to create virtual audio devices.

For Windows you can use [VB-Audio Cable](https://vb-audio.com/Cable/index.htm) or [Virtual Audio Cable](https://vac.muzychenko.net/en/) to create virtual audio devices.

## resampleaudio

The `resampleaudio` application takes an (virtual) input audio stream - for example the 8000 Hz stream from the [EAVI board](https://doi.org/10.48550/arXiv.2409.20026) - resamples it and streams it to another (virtual) output audio device.

This application makes use of [PortAudio](http://www.portaudio.com) and [Secret Rabbit Code (aka libsamplerate)](http://libsndfile.github.io/libsamplerate/).

## lsl2audio

The `lsl2audio` application takes an input LSL stream, resamples/upsamples it to a standard audio rate, and streams it to a (virtual) output audio device.

## audio2lsl

The `audio2lsl` application takes an input audio stream at an standard audio rate, for example from a (virtual) output audio device, resamples/downsamples it to an EEG rate and outputs it to an LSL stream.

## Copyrights

Copyright (C) 2022-2025, Robert Oostenveld

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
