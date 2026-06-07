export function formatUptime(sec) {
  if (!sec) return "—";
  if (sec < 60)   return `${sec}s`;
  if (sec < 3600) return `${Math.floor(sec / 60)}m`;
  return `${Math.floor(sec / 3600)}h ${Math.floor((sec % 3600) / 60)}m`;
}

const IST_OPTS = { timeZone: 'Asia/Kolkata', hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: true };
const IST_DATE_OPTS = { timeZone: 'Asia/Kolkata', day: '2-digit', month: 'short', year: 'numeric' };

export function formatTs(tsSeconds) {
  if (!tsSeconds) return "—";
  return new Date(tsSeconds * 1000).toLocaleTimeString('en-IN', IST_OPTS);
}

// Primary formatter for ms-based timestamps (Lambda + frontend both use ms)
export function formatTsMs(tsMs) {
  if (!tsMs) return "—";
  return new Date(tsMs).toLocaleTimeString('en-IN', IST_OPTS);
}

export function formatTsMsDate(tsMs) {
  if (!tsMs) return "—";
  const d = new Date(tsMs);
  return `${d.toLocaleDateString('en-IN', IST_DATE_OPTS)}, ${d.toLocaleTimeString('en-IN', IST_OPTS)}`;
}

export function formatDate(tsSeconds) {
  if (!tsSeconds) return "—";
  return new Date(tsSeconds * 1000).toLocaleDateString('en-IN', IST_DATE_OPTS);
}

export const ALERT_LABELS = {
  brute_force:       "Brute Force",
  suspicious_signin: "Suspicious Sign-in",
  high_frequency:    "High Frequency",
  unknown_user:      "Unknown User",
};

export const ML_LABELS = ["Normal", "Warning", "Critical"];
