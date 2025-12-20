use serde::Deserialize;
use std::time::Duration;

#[derive(Debug, Clone)]
pub struct PlexClient {
    server_url: String,
    token: String,
    client: reqwest::blocking::Client,
}

#[derive(Debug, Clone)]
pub struct NowPlaying {
    pub title: String,
    pub media_type: MediaType,
    pub year: Option<i32>,
    pub grandparent_title: Option<String>, // Show name for TV episodes
    pub parent_title: Option<String>,      // Season for TV, Album for music
    pub season_number: Option<i32>,        // Season number for TV
    pub episode_number: Option<i32>,       // Episode number for TV
    pub thumb_url: Option<String>,         // Full URL to thumbnail
    pub duration_ms: Option<i64>,
    pub view_offset_ms: Option<i64>,
    pub player_state: PlayerState,
}

#[derive(Debug, Clone, PartialEq)]
pub enum MediaType {
    Movie,
    Episode,
    Track,
    Unknown,
}

#[derive(Debug, Clone, PartialEq)]
pub enum PlayerState {
    Playing,
    Paused,
    Buffering,
}

// Plex API response structures
#[derive(Debug, Deserialize)]
struct SessionsResponse {
    #[serde(rename = "MediaContainer")]
    media_container: MediaContainer,
}

#[derive(Debug, Deserialize)]
struct MediaContainer {
    #[serde(rename = "Metadata", default)]
    metadata: Vec<Metadata>,
}

#[derive(Debug, Deserialize)]
struct Metadata {
    #[serde(rename = "type")]
    media_type: Option<String>,
    title: Option<String>,
    year: Option<i32>,
    #[serde(rename = "grandparentTitle")]
    grandparent_title: Option<String>,
    #[serde(rename = "parentTitle")]
    parent_title: Option<String>,
    #[serde(rename = "parentIndex")]
    season_number: Option<i32>,
    #[serde(rename = "index")]
    episode_number: Option<i32>,
    duration: Option<i64>,
    #[serde(rename = "viewOffset")]
    view_offset: Option<i64>,
    #[serde(rename = "Player")]
    player: Option<Player>,
}

#[derive(Debug, Deserialize)]
struct Player {
    state: Option<String>,
}

impl PlexClient {
    pub fn new(server_url: &str, token: &str) -> Self {
        let client = reqwest::blocking::Client::builder()
            .timeout(Duration::from_secs(10))
            .build()
            .expect("Failed to create HTTP client");

        Self {
            server_url: server_url.trim_end_matches('/').to_string(),
            token: token.to_string(),
            client,
        }
    }

    pub fn get_now_playing(&self) -> Result<Option<NowPlaying>, Box<dyn std::error::Error>> {
        let url = format!("{}/status/sessions", self.server_url);

        let response = self
            .client
            .get(&url)
            .header("X-Plex-Token", &self.token)
            .header("Accept", "application/json")
            .send()?;

        if !response.status().is_success() {
            return Err(format!("Plex API error: {}", response.status()).into());
        }

        let sessions: SessionsResponse = response.json()?;

        // Get the first active session (you could extend this to filter by user)
        let metadata = match sessions.media_container.metadata.first() {
            Some(m) => m,
            None => return Ok(None),
        };

        let media_type = match metadata.media_type.as_deref() {
            Some("movie") => MediaType::Movie,
            Some("episode") => MediaType::Episode,
            Some("track") => MediaType::Track,
            _ => MediaType::Unknown,
        };

        let player_state = match metadata.player.as_ref().and_then(|p| p.state.as_deref()) {
            Some("playing") => PlayerState::Playing,
            Some("paused") => PlayerState::Paused,
            Some("buffering") => PlayerState::Buffering,
            _ => PlayerState::Playing,
        };

        // Note: Discord Rich Presence doesn't support external image URLs
        // Images must be uploaded as assets in Discord Developer Portal
        let thumb_url: Option<String> = None;

        Ok(Some(NowPlaying {
            title: metadata.title.clone().unwrap_or_else(|| "Unknown".to_string()),
            media_type,
            year: metadata.year,
            grandparent_title: metadata.grandparent_title.clone(),
            parent_title: metadata.parent_title.clone(),
            season_number: metadata.season_number,
            episode_number: metadata.episode_number,
            thumb_url,
            duration_ms: metadata.duration,
            view_offset_ms: metadata.view_offset,
            player_state,
        }))
    }

    pub fn test_connection(&self) -> Result<(), Box<dyn std::error::Error>> {
        let url = format!("{}/", self.server_url);

        let response = self
            .client
            .get(&url)
            .header("X-Plex-Token", &self.token)
            .header("Accept", "application/json")
            .send()?;

        if response.status().is_success() {
            Ok(())
        } else {
            Err(format!("Failed to connect to Plex: {}", response.status()).into())
        }
    }
}

impl NowPlaying {
    /// Get a formatted display string for the current media
    pub fn display_title(&self) -> String {
        match self.media_type {
            MediaType::Episode => {
                if let Some(ref show) = self.grandparent_title {
                    format!("{} - {}", show, self.title)
                } else {
                    self.title.clone()
                }
            }
            MediaType::Track => {
                if let Some(ref artist) = self.grandparent_title {
                    format!("{} - {}", artist, self.title)
                } else {
                    self.title.clone()
                }
            }
            _ => {
                if let Some(year) = self.year {
                    format!("{} ({})", self.title, year)
                } else {
                    self.title.clone()
                }
            }
        }
    }

    /// Get the state text for display
    pub fn state_text(&self) -> &str {
        match self.player_state {
            PlayerState::Playing => "Playing",
            PlayerState::Paused => "Paused",
            PlayerState::Buffering => "Buffering",
        }
    }

    /// Get formatted progress string (elapsed / total)
    pub fn progress_text(&self) -> Option<String> {
        match (self.view_offset_ms, self.duration_ms) {
            (Some(offset), Some(duration)) => {
                let elapsed = format_ms(offset);
                let total = format_ms(duration);
                Some(format!("{} / {}", elapsed, total))
            }
            _ => None,
        }
    }
}

/// Format milliseconds as MM:SS or H:MM:SS
fn format_ms(ms: i64) -> String {
    let total_secs = ms / 1000;
    let hours = total_secs / 3600;
    let mins = (total_secs % 3600) / 60;
    let secs = total_secs % 60;

    if hours > 0 {
        format!("{}:{:02}:{:02}", hours, mins, secs)
    } else {
        format!("{}:{:02}", mins, secs)
    }
}
