# Pleyx

A lightweight system tray application that displays your Plex playback status as Discord Rich Presence.

![Discord Rich Presence Example](https://via.placeholder.com/300x100?text=Plex+Discord+Presence)

## Features

- Shows currently playing media in Discord profile
- Supports Movies, TV Shows, and Music
- Displays episode info (S01E05 format)
- Shows playback progress (elapsed / total)
- Auto-hides presence when paused or stopped
- Minimal resource usage (Rust-based)
- System tray with status display

## Installation

### Pre-built Binary

Download the latest release from the [Releases](../../releases) page.

### Build from Source

```bash
# Clone the repository
git clone https://github.com/yourusername/pleyx.git
cd pleyx

# Build release binary
cargo build --release

# Binary will be at target/release/pleyx.exe
```

## Configuration

On first run, a config file is created at:
- **Windows**: `%APPDATA%\pleyx\config.toml`
- **Linux**: `~/.config/pleyx/config.toml`
- **macOS**: `~/Library/Application Support/pleyx/config.toml`

Edit the config with your Plex details:

```toml
[plex]
server_url = "http://localhost:32400"
token = "YOUR_PLEX_TOKEN"

polling_interval_secs = 15
```

### Getting Your Plex Token

1. Sign in to Plex Web App
2. Browse to any media item
3. Click the "..." menu → "Get Info" → "View XML"
4. Look for `X-Plex-Token=` in the URL

Or visit: https://support.plex.tv/articles/204059436-finding-an-authentication-token-x-plex-token/

## Usage

1. Configure your Plex token in the config file
2. Run `pleyx.exe`
3. The app appears in your system tray
4. Play something on Plex - it will show in your Discord status!

### System Tray Menu

Right-click the tray icon to:
- See what's currently playing
- Open the config file
- Quit the application

## Discord Display

When watching a TV show:
```
Fallout - The Innovator
S02E01 | 25:18 / 1:02:27
```

When watching a movie:
```
Inception (2010)
1:23:45 / 2:28:00
```

## Requirements

- Windows 10/11, Linux, or macOS
- Plex Media Server (local or remote)
- Discord

## License

MIT
