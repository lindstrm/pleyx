use crate::plex::{MediaType, NowPlaying, PlayerState};
use discord_rich_presence::{activity, DiscordIpc, DiscordIpcClient};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

/// Format milliseconds as MM:SS or H:MM:SS
fn format_duration(ms: i64) -> String {
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

pub struct DiscordPresence {
    client_id: String,
    client: Option<DiscordIpcClient>,
    last_connect_attempt: Option<Instant>,
    consecutive_failures: u32,
}

const RECONNECT_DELAY_SECS: u64 = 10;
const MAX_CONSECUTIVE_FAILURES: u32 = 3;

impl DiscordPresence {
    pub fn new(client_id: &str) -> Result<Self, Box<dyn std::error::Error>> {
        Ok(Self {
            client_id: client_id.to_string(),
            client: None,
            last_connect_attempt: None,
            consecutive_failures: 0,
        })
    }

    pub fn connect(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        // Rate limit connection attempts
        if let Some(last_attempt) = self.last_connect_attempt {
            if last_attempt.elapsed() < Duration::from_secs(RECONNECT_DELAY_SECS) {
                return Err("Too soon to reconnect".into());
            }
        }

        self.last_connect_attempt = Some(Instant::now());

        // Close existing client if any
        if let Some(ref mut client) = self.client {
            let _ = client.close();
        }
        self.client = None;

        // Create new client
        tracing::info!("Connecting to Discord...");
        let mut client = DiscordIpcClient::new(&self.client_id)?;
        client.connect()?;

        self.client = Some(client);
        self.consecutive_failures = 0;
        tracing::info!("Connected to Discord");
        Ok(())
    }

    pub fn disconnect(&mut self) {
        if let Some(ref mut client) = self.client {
            let _ = client.close();
            tracing::info!("Disconnected from Discord");
        }
        self.client = None;
    }

    fn ensure_connected(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        if self.client.is_none() {
            self.connect()?;
        }
        Ok(())
    }

    pub fn update_presence(&mut self, now_playing: &NowPlaying) -> Result<(), Box<dyn std::error::Error>> {
        // Skip if too many consecutive failures
        if self.consecutive_failures >= MAX_CONSECUTIVE_FAILURES {
            if let Some(last_attempt) = self.last_connect_attempt {
                if last_attempt.elapsed() < Duration::from_secs(RECONNECT_DELAY_SECS * 3) {
                    return Err("Backing off due to repeated failures".into());
                }
            }
            // Reset and try again
            self.consecutive_failures = 0;
        }

        if let Err(e) = self.ensure_connected() {
            self.consecutive_failures += 1;
            return Err(e);
        }

        let details = now_playing.display_title();

        // Build duration string if available
        let duration_str = match (now_playing.view_offset_ms, now_playing.duration_ms) {
            (Some(offset), Some(duration)) => {
                format!("{} / {}", format_duration(offset), format_duration(duration))
            }
            _ => String::new(),
        };

        let state = match now_playing.media_type {
            MediaType::Episode => {
                // Build S01E05 format
                let episode_info = match (now_playing.season_number, now_playing.episode_number) {
                    (Some(s), Some(e)) => format!("S{:02}E{:02}", s, e),
                    (Some(s), None) => format!("S{:02}", s),
                    (None, Some(e)) => format!("E{:02}", e),
                    (None, None) => String::new(),
                };

                if !duration_str.is_empty() && !episode_info.is_empty() {
                    format!("{} | {}", episode_info, duration_str)
                } else if !episode_info.is_empty() {
                    episode_info
                } else {
                    duration_str.clone()
                }
            }
            MediaType::Track => {
                let base = now_playing.parent_title.as_deref().unwrap_or("");
                if !duration_str.is_empty() && !base.is_empty() {
                    format!("{} | {}", base, duration_str)
                } else if !base.is_empty() {
                    base.to_string()
                } else {
                    duration_str.clone()
                }
            }
            MediaType::Movie | MediaType::Unknown => {
                if !duration_str.is_empty() {
                    duration_str.clone()
                } else {
                    now_playing.state_text().to_string()
                }
            }
        };

        // Calculate timestamps for progress bar
        let timestamps = if now_playing.player_state == PlayerState::Playing {
            if let (Some(duration), Some(offset)) = (now_playing.duration_ms, now_playing.view_offset_ms) {
                let now = SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .unwrap()
                    .as_secs() as i64;

                let remaining_ms = duration - offset;
                let end_time = now + (remaining_ms / 1000);

                Some(activity::Timestamps::new().end(end_time))
            } else {
                None
            }
        } else {
            None
        };

        // Use Plex thumbnail if available, otherwise fall back to media type icon
        let (large_image, large_text) = match &now_playing.thumb_url {
            Some(url) => {
                tracing::info!("Thumb URL: {}", url);
                (url.as_str(), now_playing.display_title())
            }
            None => match now_playing.media_type {
                MediaType::Movie => ("movie", "Watching a Movie".to_string()),
                MediaType::Episode => ("tv", "Watching TV".to_string()),
                MediaType::Track => ("music", "Listening to Music".to_string()),
                MediaType::Unknown => ("plex", "Plex".to_string()),
            },
        };

        // Plex logo as small icon
        let (small_image, small_text) = ("plex", "Plex");

        let mut activity_builder = activity::Activity::new()
            .details(&details)
            .state(&state)
            .assets(
                activity::Assets::new()
                    .large_image(large_image)
                    .large_text(&large_text)
                    .small_image(small_image)
                    .small_text(small_text),
            );

        if let Some(ts) = timestamps {
            activity_builder = activity_builder.timestamps(ts);
        }

        // Try to set activity, reset connection on failure
        let client = match self.client.as_mut() {
            Some(c) => c,
            None => {
                self.consecutive_failures += 1;
                return Err("No Discord client".into());
            }
        };

        match client.set_activity(activity_builder) {
            Ok(_) => {
                self.consecutive_failures = 0;
                tracing::debug!("Updated Discord presence: {} - {}", details, state);
                Ok(())
            }
            Err(e) => {
                tracing::warn!("Failed to set activity: {}", e);
                self.consecutive_failures += 1;
                self.disconnect(); // Force reconnect next time
                Err(e.into())
            }
        }
    }

    pub fn clear_presence(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        if let Some(ref mut client) = self.client {
            if let Err(e) = client.clear_activity() {
                tracing::warn!("Failed to clear activity: {}", e);
                self.disconnect();
                return Err(e.into());
            }
            tracing::debug!("Cleared Discord presence");
        }
        Ok(())
    }

    #[allow(dead_code)]
    pub fn is_connected(&self) -> bool {
        self.client.is_some()
    }
}

impl Drop for DiscordPresence {
    fn drop(&mut self) {
        self.disconnect();
    }
}
