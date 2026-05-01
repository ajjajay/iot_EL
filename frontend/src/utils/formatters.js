export function formatUptime(sec) {
  if (!sec) return "—";
  if (sec < 60)   return `${sec}s`;
  if (sec < 3600) return `${Math.floor(sec / 60)}m`;
  return `${Math.floor(sec / 3600)}h ${Math.floor((sec % 3600) / 60)}m`;
}

export function formatTs(tsSeconds) {
  if (!tsSeconds) return "—";
  return new Date(tsSeconds * 1000).toLocaleTimeString();
}

export function formatTsMs(tsMs) {
  if (!tsMs) return "—";
  return new Date(tsMs).toLocaleTimeString();
}

export const ALERT_LABELS = {
  brute_force:       "Brute Force",
  suspicious_signin: "Suspicious Sign-in",
  high_frequency:    "High Frequency",
  unknown_user:      "Unknown User",
};

export const ML_LABELS = ["Normal", "Warning", "Critical"];
