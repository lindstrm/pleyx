# Pleyx

A lightweight Windows system tray application that displays your Plex playback status as Discord Rich Presence.

## Features

- Shows currently playing media in Discord profile
- Supports Movies, TV Shows, and Music
- Displays media artwork (OMDB posters or Plex art via catbox.moe)
- Shows IMDB and Rotten Tomatoes ratings for movies/shows (requires OMDB API key)
- Activity type changes based on media (Watching/Listening)
- Auto-hides presence when paused or stopped
- System tray with color/grayscale icon based on playback state
- Start at boot option
- Minimal resource usage

## Discord Display

**TV Shows:**
- Details: Show name
- State: S01E05 • Episode Title
- Large Image: Show poster

**Movies:**
- Details: Movie Title (Year)
- State: 7.6/10 • 85% • Genre, Genre (ratings require OMDB API key)
- Large Image: Movie poster

**Music:**
- Details: Track Title
- State: Genre
- Large Image: Album art
- Large Text: Artist - Album

## Installation

### Pre-built Binary

Download the latest release from the [Releases](../../releases) page.

### Build from Source

Requires:
- CMake 3.16+
- Visual Studio 2019+ (or MSVC Build Tools)

```bash
# Clone the repository
git clone https://github.com/yourusername/pleyx.git
cd pleyx

# Configure
cmake -S . -B build

# Build
cmake --build build --config Release

# Binary will be at build/Release/pleyx.exe
```

## Configuration

On first run, a config file is created at `%APPDATA%\pleyx\config.json`

You can also place a `config.json` in the same directory as the exe for portable mode.

```json
{
    "plex_url": "http://localhost:32400",
    "plex_token": "YOUR_PLEX_TOKEN_HERE",
    "polling_interval_secs": 15,
    "start_at_boot": false
}
```

| Option | Description |
|--------|-------------|
| `plex_url` | URL to your Plex server |
| `plex_token` | Your Plex authentication token |
| `polling_interval_secs` | How often to check for playback (seconds) |
| `start_at_boot` | Launch Pleyx when Windows starts |

### Optional Settings

These can be added manually to enable additional features:

```json
{
    "omdb_api_key": "your_omdb_api_key",
    "debug": true
}
```

| Option | Description |
|--------|-------------|
| `omdb_api_key` | OMDB API key for posters and ratings ([get one free](https://www.omdbapi.com/apikey.aspx)) |
| `debug` | Show console window with debug output |

### Getting Your Plex Token

1. Sign in to Plex Web App
2. Browse to any media item
3. Click the "..." menu → "Get Info" → "View XML"
4. Look for `X-Plex-Token=` in the URL

Or visit: https://support.plex.tv/articles/204059436-finding-an-authentication-token-x-plex-token/

## Setup

### 1. Authorize Discord Application

First, authorize the Pleyx Discord application on your account:

**[Click here to authorize Pleyx](https://discord.com/oauth2/authorize?client_id=1451961488427188355)**

### 2. Configure Plex Token

Edit the config file with your Plex token (see "Getting Your Plex Token" above).

### 3. Run Pleyx

1. Run `pleyx.exe`
2. The app appears in your system tray (grayscale icon)
3. Play something on Plex - icon turns color and Discord status updates!

### System Tray Menu

Right-click the tray icon to:
- Open Config - Edit configuration file
- Start at Boot - Toggle Windows startup
- Quit - Exit the application

## Requirements

- Windows 10/11
- Plex Media Server (local or remote)
- Discord desktop app

## License

MIT
