# Kamera

A fast, lightweight, and modern desktop photo gallery and viewer for Linux, built with **Qt 6**, **QML**, and **KDE Frameworks 6 (KF6 / KIO)**.

Kamera provides a seamless photo browsing experience with responsive edge-to-edge thumbnail grids, real-time background thumbnail generation, full-resolution image inspection with zoom and pan, video first-frame thumbnails, neighbor preloading, and native integration with KDE protocols (including **KDE Connect** phone folders and SFTP).

---

## Features

- **Responsive Grid View**: Clean, edge-to-edge photo grid that dynamically adapts column counts without trailing gaps, with smooth 1.5x wheel scrolling.
- **KIO Integration & Remote Folders**: Native support for KDE KIO URLs (including `kdeconnect://`, `sftp://`, `smb://`, and local directories).
- **Video Thumbnails & Playback**: Generates first-frame video thumbnails with an overlay play badge in the grid, and a full-size preview with a central playback trigger in the viewer.
- **Full-Resolution Viewer**:
  - Displays original full-resolution photos without downscaling or compression artifacts.
  - Interactive **zoom and pan**: mouse wheel zoom (1.0x - 5.0x), drag to pan when zoomed in, and double-click to toggle fit / 100% zoom.
  - Automatic EXIF orientation transformation for portrait phone captures.
  - Asynchronous loading with responsive cancellation on rapid navigation.
  - Keyboard navigation (`Left` / `Right`) with ESC to reset zoom or exit.
  - Shortcuts to open original media in external tools (like **Gwenview** or **mpv**).
- **Smart Preloading & Fast Navigation**: Automatically preloads neighboring media (4 ahead, 4 behind) into an LRU temporary cache (`~/.cache/kamera/full_disk_cache/`), completely cleaned up upon exit.
- **Fail-Safe Thumbnail Generation**: Tracks thumbnail generation state and prevents infinite retry loops on corrupt or unsupported files.
- **In-App & Desktop Notifications**: User feedback banners and notifications for copy, rename, delete, and connection errors.
- **Automatic Logging & Rotation**:
  - Directs logs with timestamps and severity levels to `~/.local/share/kamera/kamera.log` (capped at 5 MB, rotated to `.old`, entries older than 30 days automatically pruned).
  - Optional `--verbose` / `-v` flag (or `KAMERA_VERBOSE=1`) to echo logs to stderr.
- **Clean Lifecycle**: Fully responsive exit handling that cleanly cancels background worker jobs and releases resources on window close.

---

## Requirements & Dependencies

To build Kamera from source on Arch Linux or any Linux distribution, install:

- **Compiler**: C++17 compatible compiler (GCC or Clang)
- **Build System**: CMake (>= 3.16), Extra CMake Modules (`extra-cmake-modules`)
- **Video Processing**: `ffmpeg`
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
sudo pacman -S --needed base-devel cmake extra-cmake-modules ffmpeg \
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

To run with verbose terminal output:

```bash
./build/kamera --verbose
```

### 3. Install System-Wide (Optional)

```bash
sudo cmake --install build
```

Once installed, you can launch `kamera` directly from the terminal or your application launcher.

---

## Application Data & Logs

- **Database**: SQLite index with WAL mode stored at `~/.local/share/kamera/kamera.db`.
- **Thumbnails**: Grid thumbnails (320px) cached in `~/.cache/kamera/thumbs/`.
- **Logs**: Process logs and diagnostics are stored at `~/.local/share/kamera/kamera.log`.

---

## License

This project is licensed under the [MIT License](LICENSE).
