# ISS Tactical Tracker

Native C++17 / Qt 6 ISS tracker for Raspberry Pi 5 and Linux desktops.

## Features

- Live ISS telemetry and UTC clock
- 90-minute red recent track and 30-minute amber forecast
- Offline SVG world map
- Location selection and visible-pass forecast
- Tactical compass and CRT-inspired interface
- HTTPS protections and cache validation

## Debian and Raspberry Pi OS

```sh
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-svg-dev git

git clone https://github.com/tsikibrikitambet-dotcom/iss-tracker-qt.git
cd iss-tracker-qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/iss_tracker
```

## Arch Linux

```sh
sudo pacman -Syu
sudo pacman -S base-devel cmake qt6-base qt6-svg git

git clone https://github.com/tsikibrikitambet-dotcom/iss-tracker-qt.git
cd iss-tracker-qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/iss_tracker
```

## Tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## License

MIT License.
