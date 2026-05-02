import { useState } from 'react';

const NAV_ITEMS = [
  { id: 'dashboard', icon: '⬡', label: 'Dashboard' },
  { id: 'devices',   icon: '⊞', label: 'Devices' },
  { id: 'activity',  icon: '✎', label: 'Activity' },
  { id: 'settings',  icon: '⚙', label: 'Settings' },
];

export default function Sidebar({ anomalyCount }) {
  const [active, setActive] = useState('dashboard');

  return (
    <aside className="sidebar">
      <div className="sidebar-top">
        <div className="sidebar-logo">🏠</div>
        <nav className="sidebar-nav">
          {NAV_ITEMS.map(item => (
            <button
              key={item.id}
              className={`sidebar-btn${active === item.id ? ' sidebar-btn-active' : ''}`}
              onClick={() => setActive(item.id)}
              title={item.label}
            >
              <span className="sidebar-btn-icon">{item.icon}</span>
              <span className="sidebar-btn-label">{item.label}</span>
              {item.id === 'activity' && anomalyCount > 0 && (
                <span className="sidebar-badge">{anomalyCount}</span>
              )}
            </button>
          ))}
        </nav>
      </div>
      <button className="sidebar-btn sidebar-logout" title="Logout">
        <span className="sidebar-btn-icon">⏻</span>
        <span className="sidebar-btn-label">Logout</span>
      </button>
    </aside>
  );
}
