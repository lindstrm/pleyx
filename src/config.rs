use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    pub plex: PlexConfig,
    #[serde(default = "default_polling_interval")]
    pub polling_interval_secs: u64,
}

fn default_polling_interval() -> u64 {
    15
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PlexConfig {
    pub server_url: String,
    pub token: String,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            plex: PlexConfig {
                server_url: "http://localhost:32400".to_string(),
                token: "YOUR_PLEX_TOKEN_HERE".to_string(),
            },
            polling_interval_secs: 15,
        }
    }
}

impl Config {
    pub fn config_path() -> PathBuf {
        let config_dir = dirs::config_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("pleyx");
        config_dir.join("config.toml")
    }

    pub fn load() -> Result<Self, Box<dyn std::error::Error>> {
        let path = Self::config_path();

        if !path.exists() {
            // Create default config
            let config = Config::default();
            config.save()?;
            return Err(format!(
                "Config file created at {:?}. Please edit it with your Plex server URL and token.",
                path
            ).into());
        }

        let content = fs::read_to_string(&path)?;
        let config: Config = toml::from_str(&content)?;
        Ok(config)
    }

    pub fn save(&self) -> Result<(), Box<dyn std::error::Error>> {
        let path = Self::config_path();

        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }

        let content = toml::to_string_pretty(self)?;
        fs::write(&path, content)?;
        Ok(())
    }
}
