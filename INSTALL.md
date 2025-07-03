# Installing

You can download a compiled version of this software from <https://github.com/robertoostenveld/resampleaudio/releases>.

## Compiling the software

To compile these applications yourself, you will need [cmake](https://cmake.org) and the following libraries:

- <http://www.portaudio.com> and <http://libsndfile.github.io/libsamplerate> for all three applications
- <https://labstreaminglayer.readthedocs.io> for `lsl2audio` and `audio2lsl`.

You can install these with your platform-specific package manager (homebrew, apt, yum), after which they will end up in `/usr/local/lib` and `/usr/local/include` or similar system-wide directories. You can also install them manually in the `external` directory. In that case the directory layout should be

```console
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

You can subsequently compile the software like this:

```console
mkdir build
cd build
cmake ..
cmake --build .
```
