export default function Navbar({ theme, onToggleTheme, onRefresh, anomalyCount, devicesOnline, devicesTotal }) {
  let statusText  = '● Live';
  let statusClass = 'status-badge status-ok';
  if (anomalyCount > 0) {
    statusText  = `⚠ ${anomalyCount} Alert${anomalyCount > 1 ? 's' : ''}`;
    statusClass = 'status-badge status-err';
  } else if (devicesOnline < devicesTotal) {
    statusText  = 'Partial';
    statusClass = 'status-badge status-warn';
  }

  return (
    <nav className="navbar">
      <div className="nav-brand">
        <span className="nav-icon">🏠</span>
        <span className="nav-title">Iris Biometric AC</span>
        <span className="nav-version">v2.0</span>
      </div>
      <div className="nav-right">
        <span className={statusClass}>{statusText}</span>
        <button className="btn btn-ghost" title="Toggle dark/light mode" onClick={onToggleTheme}>
          {theme === 'dark' ? '☾' : '☀'}
        </button>
        <button className="btn btn-ghost" title="Force refresh" onClick={onRefresh}>↻</button>
      </div>
    </nav>
  );
}
