use serde_json::{json, Value};
use std::{
    io::{BufRead, BufReader, Write},
    path::{Path, PathBuf},
    process::{Child, ChildStdin, ChildStdout, Command, Stdio},
    sync::Mutex,
};

struct Bridge {
    _child: Child,
    stdin: ChildStdin,
    stdout: BufReader<ChildStdout>,
}

pub struct TorrentManager {
    bridge: Mutex<Bridge>,
}

impl TorrentManager {
    pub fn start(app_resource_dir: &Path, state_dir: &Path) -> Result<Self, String> {
        let mut candidates = vec![app_resource_dir.join("qdm-torrent-engine.exe")];
        if let Ok(exe) = std::env::current_exe() {
            if let Some(parent) = exe.parent() {
                candidates.push(parent.join("qdm-torrent-engine.exe"));
                candidates.push(parent.join("resources").join("qdm-torrent-engine.exe"));
            }
        }
        let helper: PathBuf = candidates
            .into_iter()
            .find(|p| p.is_file())
            .ok_or_else(|| "The libtorrent helper executable is not installed in this build.".to_string())?;

        let mut child = Command::new(&helper)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .map_err(|e| format!("Could not start libtorrent helper {}: {e}", helper.display()))?;

        let stdin = child.stdin.take().ok_or("Torrent helper stdin unavailable")?;
        let stdout = child.stdout.take().ok_or("Torrent helper stdout unavailable")?;
        let mut manager = Self {
            bridge: Mutex::new(Bridge {
                _child: child,
                stdin,
                stdout: BufReader::new(stdout),
            }),
        };
        let state_file = state_dir.join("torrents.json").to_string_lossy().to_string();
        manager.request(json!({"command":"configure", "stateFile": state_file}))?;
        let pong = manager.request(json!({"command":"ping"}))?;
        if pong["ok"] != true {
            return Err(pong["error"].as_str().unwrap_or("Torrent helper failed its handshake").to_string());
        }
        Ok(manager)
    }

    pub fn request(&self, request: Value) -> Result<Value, String> {
        let mut bridge = self.bridge.lock().map_err(|_| "Torrent engine lock poisoned".to_string())?;
        serde_json::to_writer(&mut bridge.stdin, &request)
            .map_err(|e| format!("Could not encode torrent command: {e}"))?;
        bridge.stdin.write_all(b"\n").map_err(|e| format!("Could not send torrent command: {e}"))?;
        bridge.stdin.flush().map_err(|e| format!("Could not flush torrent command: {e}"))?;

        let mut line = String::new();
        let read = bridge.stdout.read_line(&mut line)
            .map_err(|e| format!("Could not read torrent engine response: {e}"))?;
        if read == 0 {
            return Err("Torrent engine exited unexpectedly.".to_string());
        }
        let response: Value = serde_json::from_str(&line)
            .map_err(|e| format!("Invalid torrent engine response: {e}"))?;
        if response["ok"] != true {
            return Err(response["error"].as_str().unwrap_or("Torrent engine command failed").to_string());
        }
        Ok(response)
    }
}

impl Drop for TorrentManager {
    fn drop(&mut self) {
        if let Ok(mut bridge) = self.bridge.lock() {
            let _ = serde_json::to_writer(&mut bridge.stdin, &json!({"command":"shutdown"}));
            let _ = bridge.stdin.write_all(b"\n");
            let _ = bridge.stdin.flush();
            let _ = bridge._child.wait();
        }
    }
}
