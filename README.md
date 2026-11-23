# ISS Tactical Tracker

![ISS Tactical Tracker](assets/iss-tracker-github.png)



A native C++17 ISS tracker for Raspberry Pi 5 and desktop Linux. It uses Qt 6 and an
embedded offline world map, with live HTTPS telemetry and a tactical CRT-style interface.


## Features


- Minimal map-first tactical display with UTC, speed, altitude, location, and no title bar text.
- Historical orbit is red; predicted orbit is amber; an analog compass shows ISS travel direction.
- Uses CRT scanline and phosphor visual effects plus live AOS countdown for the next ISS overflight.
- Tactical military-style dark display with TLS/offline status and restrained amber/olive markings.
- Displays the ISS position, speed, altitude, and recent/projected ground track.
- Keeps 90 minutes of actual history in green, extends it with live fixes, and shows the next 30 minutes in amber.
- Keeps the ISS centered horizontally so the orbit remains continuous across the map seam.
- Keeps the projected orbit anchored to live telemetry and joins it to the recent ground track.
- Animates the ISS marker in its direction of travel.
- Set your location by clicking the map or searching for a city/place name.
- Shows next visible ISS overflights for the saved location in local time.
- Saves the selected location immediately, restores it on the next launch, and caches recent telemetry.
- Stable live window with a fixed 960 x 600 map-first layout.
- Prevents duplicate app instances and blocks HTTP downgrade redirects.
- Uses a Release build profile for Raspberry Pi 5 and aborts stalled or invalid TLS/network responses.
- Uses Estonian decimal formatting and caches the rendered offline map for lower Raspberry Pi 5 CPU use.
- Includes an automated orbit-segment test; run with `ctest --test-dir build --output-on-failure`.


The map is bundled with the application and works offline. Live telemetry, place-name
search, and pass forecasts require an internet connection.


## Install on Debian or Raspberry Pi OS


```sh
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-svg-dev git


git clone https://github.com/tsikibrikitambet-dotcom/iss-tracker-qt.git# ISS Tactical Tracker

A native C++17 ISS tracker for Raspberry Pi 5 and desktop Linux. It uses Qt 6 and an
embedded offline world map, with live HTTPS telemetry and a tactical CRT-style interface.

## Features

- Minimal map-first tactical display with UTC, speed, altitude, location, and no title bar text.
- Historical orbit is red; predicted orbit is amber; an analog compass shows ISS travel direction.
- Uses CRT scanline and phosphor visual effects plus live AOS countdown for the next ISS overflight.
- Tactical military-style dark display with TLS/offline status and restrained amber/olive markings.
- Displays the ISS position, speed, altitude, and recent/projected ground track.
- Keeps 90 minutes of actual history in green, extends it with live fixes, and shows the next 30 minutes in amber.
- Keeps the ISS centered horizontally so the orbit remains continuous across the map seam.
- Keeps the projected orbit anchored to live telemetry and joins it to the recent ground track.
- Animates the ISS marker in its direction of travel.
- Set your location by clicking the map or searching for a city/place name.
- Shows next visible ISS overflights for the saved location in local time.
- Saves the selected location immediately, restores it on the next launch, and caches recent telemetry.
- Stable live window with a fixed 960 x 600 map-first layout.
- Prevents duplicate app instances and blocks HTTP downgrade redirects.
- Uses a Release build profile for Raspberry Pi 5 and aborts stalled or invalid TLS/network responses.
- Uses Estonian decimal formatting and caches the rendered offline map for lower Raspberry Pi 5 CPU use.
- Includes an automated orbit-segment test; run with `ctest --test-dir build --output-on-failure`.

The map is bundled with the application and works offline. Live telemetry, place-name
search, and pass forecasts require an internet connection.

## Install on Debian or Raspberry Pi OS

```sh
sudo apt update
sudo apt install build-essential cmake qt6-base-dev qt6-svg-dev git

git clone https://github.com/tsikibrikitambet-dotcom/iss-tracker-qt.git
cd iss-tracker-qt

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/iss_tracker
```

For an executable named `iss_tracker` available in the terminal:

```sh
install -Dm755 build/iss_tracker ~/.local/bin/iss_tracker
iss_tracker
```

## Install on Arch Linux

```sh
sudo pacman -Syu
sudo pacman -S base-devel cmake qt6-base qt6-svg git

git clone https://github.com/tsikibrikitambet-dotcom/iss-tracker-qt.git
cd iss-tracker-qt

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/iss_tracker
```

Optional installation to the user binary directory:

```sh
install -Dm755 build/iss_tracker ~/.local/bin/iss_tracker
iss_tracker
```

## Run tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Controls

- Click the map to set your home location.
- Search for a city or place name.
- The red line is the ISS's recent track.
- The amber line predicts the next 30 minutes.
- The compass needle shows travel direction.
- The UTC clock and “AOS” countdown use live pass forecasts.

ISS telemetry is provided by [Where the ISS at?](https://wheretheiss.at/).
Place-name search uses [OpenStreetMap Nominatim](https://nominatim.openstreetmap.org/).
