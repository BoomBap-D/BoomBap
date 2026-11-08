# BoomBap

A hip-hop beat-making app (standalone, JUCE/C++) built from original models of the classic gear:
808-style drums, MPC-style pads and swing, S950-style crunch, SSL-style channel strip and bus
compressor, and a vocal chain with mic-type models. These are approximations of each gear's
general character, not copies of any product.

## Build
Needs CMake 3.22+, a C++17 compiler, and Git. JUCE (7.0.12) is downloaded automatically.

    cmake -B build
    cmake --build build --config Release

Linux also needs the usual JUCE packages: libasound2-dev, libx11-dev, libxrandr-dev,
libxinerama-dev, libxcursor-dev, libfreetype-dev, libfontconfig1-dev, libgl1-mesa-dev.

Run the tests (no audio device needed):

    ctest --test-dir build

## Use
- Click steps to cycle normal / accent / off. Right-click clears. Space plays and stops.
- Load... on a Sample row loads a WAV/AIFF/MP3/FLAC/OGG (up to 20 s).
- Dust = S950-style sample-rate and bit crush. Glue = bus compressor.
- Vocal monitor uses your microphone. Use headphones to avoid feedback.
- Export WAV renders 1/2/4/8 bars (24-bit stereo, plus a 2 s tail). The vocal is not included yet.
- Projects save as readable .boombap files (Ctrl/Cmd+S).

## Layout
    Source/Core/       DSP, no JUCE: Drums808, PadsMPC, Sampler950, ChannelStrip, VocalChain, Common
    Source/App/        the standalone: engine, window, project file, WAV export
    Tests/             headless tests and demo renders (*.wav)
    Examples/          example.boombap

## Not done yet
Vocal recording, piano roll/playlist, pad chop controls in the UI, plugin (VST3/AU) wrappers,
recent-files list. Check licensing before shipping: JUCE has its own terms, and use only samples
and impulse responses you own or have licensed.
