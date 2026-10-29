# Kamera

A fast, lightweight, and modern desktop photo gallery and viewer for Linux, built with **Qt 6**, **QML**, and **KDE Frameworks 6 (KF6 / KIO)**.

Kamera provides a seamless photo browsing experience with edge-to-edge thumbnail grids, real-time background thumbnail generation, full-resolution image inspection, neighbor preloading, and full integration with KDE protocols (including **KDE Connect** device folders and SFTP).

---

## Features

- **Modern Grid View**: Clean, edge-to-edge photo grid with smooth scrolling and responsive 1.5x wheel navigation.
- **KIO Integration & Remote Folders**: Native support for KDE KIO URLs (including `kdeconnect://`, `sftp://`, `smb://`, and local directories).
- **Instant Live Thumbnails**: Multi-threaded thumbnail generation; thumbnails appear dynamically as they are processed without needing manual rescans.
- **Full-Resolution Viewer**:
  - Displays original full-resolution photos without blurriness or downscaling artifacts.
  - Asynchronous loading for both local and remote files.
  - Keyboard arrow navigation (`Left` / `Right`) with ESC to exit.
  - One-click / double-click shortcut to open original photos in external viewers (like **Gwenview**).
- **Smart Preloading & Fast Navigation**: Automatically preloads neighboring photos (4 ahead, 4 behind) into a temporary disk cache so swiping through photos is instantaneous.
- **Low Memory Footprint**: Keeps RAM usage minimal by caching rendered full-res images in an LRU temporary cache (`~/.cache/kamera/full_disk_cache/`), completely cleaned up upon exit.
- **Automatic Logging & Rotation**:
  - Suppresses terminal noise and directs logs with timestamps and severity levels to `~/.local/share/kamera/kamera.log`.
  - Automatic 5 MB rotation cap and 30-day age retention policy.
- **Clean Lifecycle**: Fully responsive exit handling that cleanly cancels background worker jobs and releases resources on window close.

---

## Requirements & Dependencies

To build Kamera from source on Arch Linux or any Linux distribution, install:

- **Compiler**: C++17 compatible compiler (GCC or Clang)
- **Build System**: CMake (>= 3.16), Extra CMake Modules (`extra-cmake-modules`)
- **Qt 6**:
  - `qt6-base` (Core, Gui, Widgets, Sql, Concurrent)
  - `qt6-declarative` (Quick, QML)
  - `qt6-quickcontrols2`
- **KDE Frameworks 6 (KF6)**:
  - `kio` (KIOCore, KIOGui, KIOWidgets, KIOFileWidgets)
  - `ki18n`
  - `knotifications`

### Installing Dependencies (Arch Linux)

```bash
sudo pacman -S --needed base-devel cmake extra-cmake-modules \
    qt6-base qt6-declarative qt6-quickcontrols2 \
    kio ki18n knotifications
```

---

## Building and Installing

### 1. Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 2. Run Directly

```bash
./build/kamera
```

### 3. Install System-Wide (Optional)

```bash
sudo cmake --install build
```

Once installed, you can launch `kamera` directly from the terminal or your application launcher.

---

## Application Data & Logs

- **Database**: SQLite index is stored at `~/.local/share/kamera/kamera.db`.
- **Thumbnails**: Grid thumbnails are cached in `~/.cache/kamera/thumbs/`.
- **Logs**: Process logs and diagnostics are stored at `~/.local/share/kamera/kamera.log` (capped at 5 MB, rotated to `.old`, entries older than 30 days automatically pruned).

---

## License

This project is licensed under the [MIT License](LICENSE).
