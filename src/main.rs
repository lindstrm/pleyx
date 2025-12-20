// Uncomment to hide console window in release:
// #![windows_subsystem = "windows"]

mod config;
mod discord;
mod plex;

use config::Config;
use discord::DiscordPresence;
use plex::PlexClient;

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

use tao::event_loop::{ControlFlow, EventLoopBuilder};
use tray_icon::{
    menu::{Menu, MenuEvent, MenuItem, PredefinedMenuItem},
    TrayIconBuilder,
};

fn main() {
    // Initialize logging
    tracing_subscriber::fmt()
        .with_max_level(tracing::Level::DEBUG)
        .with_target(false)
        .init();

    tracing::info!("Pleyx starting...");

    // Load configuration
    let config = match Config::load() {
        Ok(cfg) => cfg,
        Err(e) => {
            let msg = format!("Configuration error: {}\n\nConfig file location: {:?}", e, Config::config_path());
            show_error_dialog("Pleyx - Configuration Error", &msg);
            return;
        }
    };

    // Test Plex connection
    let plex_client = PlexClient::new(&config.plex.server_url, &config.plex.token);
    if let Err(e) = plex_client.test_connection() {
        let msg = format!("Failed to connect to Plex server: {}\n\nPlease check your config at {:?}", e, Config::config_path());
        show_error_dialog("Pleyx - Connection Error", &msg);
        return;
    }

    tracing::info!("Connected to Plex server");

    // Create event loop
    let event_loop = EventLoopBuilder::new().build();

    // Create tray menu
    let menu = Menu::new();
    let now_playing_item = MenuItem::new("Nothing playing", false, None);
    let separator1 = PredefinedMenuItem::separator();
    let open_config_item = MenuItem::new("Open Config", true, None);
    let quit_item = MenuItem::new("Quit", true, None);

    menu.append(&now_playing_item).unwrap();
    menu.append(&separator1).unwrap();
    menu.append(&open_config_item).unwrap();
    menu.append(&quit_item).unwrap();

    // Channel for sending now playing updates from poll thread to main thread
    let (status_tx, status_rx) = mpsc::channel::<Option<String>>();

    // Create tray icon
    let icon = create_default_icon();
    let _tray_icon = TrayIconBuilder::new()
        .with_menu(Box::new(menu))
        .with_tooltip("Pleyx - Plex Discord Presence")
        .with_icon(icon)
        .build()
        .expect("Failed to create tray icon");

    // Menu event receiver
    let menu_channel = MenuEvent::receiver();

    // Store item IDs for comparison
    let quit_id = quit_item.id().clone();
    let open_config_id = open_config_item.id().clone();

    // Running flag for the background thread
    let running = Arc::new(AtomicBool::new(true));
    let running_clone = running.clone();

    // Spawn background thread for Plex polling and Discord updates
    let polling_interval = config.polling_interval_secs;
    const DISCORD_CLIENT_ID: &str = "1451961488427188355";

    let _poll_thread = thread::spawn(move || {
        let mut discord = match DiscordPresence::new(DISCORD_CLIENT_ID) {
            Ok(d) => d,
            Err(e) => {
                tracing::error!("Failed to create Discord client: {}", e);
                return;
            }
        };

        let mut last_playing: Option<String> = None;

        while running_clone.load(Ordering::Relaxed) {
            match plex_client.get_now_playing() {
                Ok(Some(now_playing)) => {
                    let progress = now_playing.progress_text().unwrap_or_default();
                    let display = if progress.is_empty() {
                        format!("{} ({})", now_playing.display_title(), now_playing.state_text())
                    } else {
                        format!("{} [{}]", now_playing.display_title(), progress)
                    };
                    let current_key = format!("{}-{:?}-{}", now_playing.title, now_playing.player_state, progress);

                    // Only update menu if something changed
                    if last_playing.as_ref() != Some(&current_key) {
                        tracing::info!("Now playing: {}", now_playing.display_title());
                        last_playing = Some(current_key);
                        let _ = status_tx.send(Some(display));
                    }

                    // Only show Discord presence when actively playing
                    if now_playing.player_state == plex::PlayerState::Playing {
                        match discord.update_presence(&now_playing) {
                            Ok(_) => tracing::info!("Discord presence updated successfully"),
                            Err(e) => tracing::warn!("Discord update failed: {}", e),
                        }
                    } else {
                        // Clear presence when paused/stopped
                        let _ = discord.clear_presence();
                    }
                }
                Ok(None) => {
                    if last_playing.is_some() {
                        tracing::info!("Nothing playing");
                        last_playing = None;
                        let _ = status_tx.send(None);
                        let _ = discord.clear_presence();
                    }
                }
                Err(e) => {
                    tracing::warn!("Failed to get Plex status: {}", e);
                }
            }

            // Sleep in small increments to allow faster shutdown
            for _ in 0..polling_interval {
                if !running_clone.load(Ordering::Relaxed) {
                    break;
                }
                thread::sleep(Duration::from_secs(1));
            }
        }

        discord.disconnect();
        tracing::info!("Background thread stopped");
    });

    // Run the event loop - use WaitUntil to avoid blocking the message pump
    event_loop.run(move |_event, _target, control_flow| {
        // Schedule next wake-up in 200ms (non-blocking)
        *control_flow = ControlFlow::WaitUntil(Instant::now() + Duration::from_millis(200));

        // Check for status updates from the poll thread
        while let Ok(status) = status_rx.try_recv() {
            match status {
                Some(text) => now_playing_item.set_text(&text),
                None => now_playing_item.set_text("Nothing playing"),
            }
        }

        // Handle menu events
        while let Ok(event) = menu_channel.try_recv() {
            if event.id == quit_id {
                tracing::info!("Quit requested");
                running.store(false, Ordering::Relaxed);
                *control_flow = ControlFlow::Exit;
            } else if event.id == open_config_id {
                let config_path = Config::config_path();
                if let Err(e) = open::that(&config_path) {
                    tracing::error!("Failed to open config file: {}", e);
                }
            }
        }
    });
}

fn create_default_icon() -> tray_icon::Icon {
    // Create a simple 32x32 orange/Plex-colored icon
    let size = 32u32;
    let mut rgba = Vec::with_capacity((size * size * 4) as usize);

    for y in 0..size {
        for x in 0..size {
            let cx = (x as f32) - (size as f32 / 2.0);
            let cy = (y as f32) - (size as f32 / 2.0);
            let dist = (cx * cx + cy * cy).sqrt();

            if dist < (size as f32 / 2.0 - 2.0) {
                // Plex orange color
                rgba.push(229); // R
                rgba.push(160); // G
                rgba.push(13);  // B
                rgba.push(255); // A
            } else {
                // Transparent
                rgba.push(0);
                rgba.push(0);
                rgba.push(0);
                rgba.push(0);
            }
        }
    }

    tray_icon::Icon::from_rgba(rgba, size, size).expect("Failed to create icon")
}

#[cfg(windows)]
fn show_error_dialog(title: &str, message: &str) {
    use std::ffi::OsStr;
    use std::iter::once;
    use std::os::windows::ffi::OsStrExt;
    use std::ptr::null_mut;

    fn to_wide(s: &str) -> Vec<u16> {
        OsStr::new(s).encode_wide().chain(once(0)).collect()
    }

    #[link(name = "user32")]
    unsafe extern "system" {
        fn MessageBoxW(hwnd: *mut (), text: *const u16, caption: *const u16, utype: u32) -> i32;
    }

    let text = to_wide(message);
    let caption = to_wide(title);
    unsafe {
        MessageBoxW(null_mut(), text.as_ptr(), caption.as_ptr(), 0x10); // MB_ICONERROR
    }
}

#[cfg(not(windows))]
fn show_error_dialog(_title: &str, message: &str) {
    eprintln!("{}", message);
}
