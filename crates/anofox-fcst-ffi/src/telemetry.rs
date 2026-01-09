//! PostHog telemetry integration for anonymous usage tracking.
//!
//! This module provides opt-out telemetry for collecting anonymous usage statistics.
//! Users can disable telemetry via:
//! - Environment variable: DATAZOO_DISABLE_TELEMETRY=1
//! - SQL setting: SET anofox_telemetry_enabled = false;
//!
//! Telemetry is automatically disabled in CI environments (detected via common CI
//! environment variables like CI, GITHUB_ACTIONS, GITLAB_CI, etc.).

use std::env;
use std::ffi::{c_char, CStr};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::OnceLock;

#[cfg(feature = "telemetry")]
use std::thread;

/// Global telemetry enabled flag
static TELEMETRY_ENABLED: AtomicBool = AtomicBool::new(true);

/// PostHog API key
static TELEMETRY_KEY: OnceLock<String> = OnceLock::new();

/// Anonymous distinct ID (generated once per session)
#[cfg(feature = "telemetry")]
static DISTINCT_ID: OnceLock<String> = OnceLock::new();

/// Detects if the code is running in a CI environment.
///
/// Checks for common CI environment variables set by popular CI systems:
/// - `CI` - Generic flag used by GitHub Actions, GitLab CI, Travis CI, CircleCI, etc.
/// - `GITHUB_ACTIONS` - GitHub Actions
/// - `GITLAB_CI` - GitLab CI
/// - `CIRCLECI` - CircleCI
/// - `TRAVIS` - Travis CI
/// - `JENKINS_URL` - Jenkins
/// - `BUILDKITE` - Buildkite
/// - `TEAMCITY_VERSION` - TeamCity
/// - `TF_BUILD` - Azure Pipelines
/// - `CODEBUILD_BUILD_ID` - AWS CodeBuild
pub fn is_ci_environment() -> bool {
    const CI_ENV_VARS: &[&str] = &[
        "CI",
        "GITHUB_ACTIONS",
        "GITLAB_CI",
        "CIRCLECI",
        "TRAVIS",
        "JENKINS_URL",
        "BUILDKITE",
        "TEAMCITY_VERSION",
        "TF_BUILD",
        "CODEBUILD_BUILD_ID",
    ];

    CI_ENV_VARS.iter().any(|var| env::var(var).is_ok())
}

/// Initialize telemetry with the given settings.
///
/// # Arguments
/// * `enabled` - Whether telemetry is enabled
/// * `api_key` - Optional PostHog API key
pub fn init_telemetry(enabled: bool, api_key: Option<&str>) {
    // Check environment variable first
    if env::var("DATAZOO_DISABLE_TELEMETRY").is_ok() {
        TELEMETRY_ENABLED.store(false, Ordering::SeqCst);
        return;
    }

    // Disable telemetry in CI environments
    if is_ci_environment() {
        TELEMETRY_ENABLED.store(false, Ordering::SeqCst);
        return;
    }

    TELEMETRY_ENABLED.store(enabled, Ordering::SeqCst);

    if let Some(key) = api_key {
        if !key.is_empty() {
            let _ = TELEMETRY_KEY.set(key.to_string());
        }
    }

    // Generate anonymous distinct_id
    #[cfg(feature = "telemetry")]
    {
        let _ = DISTINCT_ID.set(uuid::Uuid::new_v4().to_string());
    }
}

/// Check if telemetry is enabled.
pub fn is_enabled() -> bool {
    TELEMETRY_ENABLED.load(Ordering::SeqCst)
}

/// Capture a telemetry event.
///
/// Events are sent asynchronously to avoid blocking the main thread.
#[cfg(feature = "telemetry")]
pub fn capture_event(event: &str, properties: serde_json::Value) {
    if !is_enabled() {
        return;
    }

    let api_key = match TELEMETRY_KEY.get() {
        Some(key) if !key.is_empty() => key.clone(),
        _ => return, // No API key configured
    };

    let distinct_id = DISTINCT_ID.get().cloned().unwrap_or_default();
    let event_name = event.to_string();

    // Spawn a thread to send the event asynchronously
    thread::spawn(move || {
        let payload = serde_json::json!({
            "api_key": api_key,
            "event": event_name,
            "properties": {
                "$lib": "anofox-forecast-rust",
                "$lib_version": env!("CARGO_PKG_VERSION"),
            },
            "distinct_id": distinct_id,
        });

        // Merge additional properties
        let Some(payload_obj) = payload.as_object() else {
            return; // Silently skip if payload is not an object
        };
        let mut payload_obj = payload_obj.clone();
        if let Some(props) = properties.as_object() {
            if let Some(existing_props) = payload_obj.get_mut("properties") {
                if let Some(existing_props_obj) = existing_props.as_object_mut() {
                    for (k, v) in props {
                        existing_props_obj.insert(k.clone(), v.clone());
                    }
                }
            }
        }

        let _ = ureq::post("https://app.posthog.com/capture")
            .set("Content-Type", "application/json")
            .send_json(serde_json::Value::Object(payload_obj));
    });
}

/// No-op capture when telemetry feature is disabled.
/// This version accepts any type that can be ignored.
#[cfg(not(feature = "telemetry"))]
pub fn capture_event<T>(_event: &str, _properties: T) {
    // No-op when telemetry feature is disabled
}

/// Capture extension load event.
pub fn capture_extension_load() {
    #[cfg(feature = "telemetry")]
    {
        let properties = serde_json::json!({
            "extension": "anofox_forecast",
            "version": env!("CARGO_PKG_VERSION"),
            "platform": std::env::consts::OS,
            "arch": std::env::consts::ARCH,
        });
        capture_event("extension_loaded", properties);
    }
}

// ============================================================================
// FFI Exports
// ============================================================================

/// Initialize telemetry from C/C++.
///
/// # Safety
/// The api_key pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_telemetry_init(enabled: bool, api_key: *const c_char) {
    let key = if api_key.is_null() {
        None
    } else {
        CStr::from_ptr(api_key).to_str().ok()
    };
    init_telemetry(enabled, key);
}

/// Check if telemetry is enabled.
#[no_mangle]
pub extern "C" fn anofox_telemetry_is_enabled() -> bool {
    is_enabled()
}

/// Capture extension load event from C/C++.
#[no_mangle]
pub extern "C" fn anofox_telemetry_capture_extension_load() {
    capture_extension_load();
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    // Mutex to ensure environment variable tests run serially
    static ENV_MUTEX: Mutex<()> = Mutex::new(());

    /// List of CI environment variables we check for
    const CI_ENV_VARS: &[&str] = &[
        "CI",
        "GITHUB_ACTIONS",
        "GITLAB_CI",
        "CIRCLECI",
        "TRAVIS",
        "JENKINS_URL",
        "BUILDKITE",
        "TEAMCITY_VERSION",
        "TF_BUILD",
        "CODEBUILD_BUILD_ID",
    ];

    /// Helper to save current CI env var state
    fn save_ci_env_state() -> Vec<(&'static str, Option<String>)> {
        CI_ENV_VARS
            .iter()
            .map(|&var| (var, env::var(var).ok()))
            .collect()
    }

    /// Helper to restore CI env var state
    fn restore_ci_env_state(state: Vec<(&'static str, Option<String>)>) {
        for (var, value) in state {
            match value {
                Some(v) => env::set_var(var, v),
                None => env::remove_var(var),
            }
        }
    }

    #[test]
    fn test_is_ci_environment_detects_ci_var() {
        let _guard = ENV_MUTEX.lock().unwrap();
        let saved_state = save_ci_env_state();

        // Clear all CI vars first
        for var in CI_ENV_VARS {
            env::remove_var(var);
        }

        // With no CI vars set, should return false
        assert!(
            !is_ci_environment(),
            "Should not detect CI when no vars set"
        );

        // Test that setting CI=true is detected
        env::set_var("CI", "true");
        assert!(is_ci_environment(), "Should detect CI when CI=true");
        env::remove_var("CI");

        restore_ci_env_state(saved_state);
    }

    #[test]
    fn test_is_ci_environment_detects_github_actions() {
        let _guard = ENV_MUTEX.lock().unwrap();
        let saved_state = save_ci_env_state();

        for var in CI_ENV_VARS {
            env::remove_var(var);
        }

        env::set_var("GITHUB_ACTIONS", "true");
        assert!(is_ci_environment(), "Should detect GitHub Actions");
        env::remove_var("GITHUB_ACTIONS");

        restore_ci_env_state(saved_state);
    }

    #[test]
    fn test_is_ci_environment_detects_gitlab_ci() {
        let _guard = ENV_MUTEX.lock().unwrap();
        let saved_state = save_ci_env_state();

        for var in CI_ENV_VARS {
            env::remove_var(var);
        }

        env::set_var("GITLAB_CI", "true");
        assert!(is_ci_environment(), "Should detect GitLab CI");
        env::remove_var("GITLAB_CI");

        restore_ci_env_state(saved_state);
    }

    #[test]
    fn test_is_ci_environment_empty_value_still_detected() {
        let _guard = ENV_MUTEX.lock().unwrap();
        let saved_state = save_ci_env_state();

        for var in CI_ENV_VARS {
            env::remove_var(var);
        }

        // Even an empty value means the var is set
        env::set_var("CI", "");
        assert!(
            is_ci_environment(),
            "Should detect CI even with empty value"
        );
        env::remove_var("CI");

        restore_ci_env_state(saved_state);
    }
}
