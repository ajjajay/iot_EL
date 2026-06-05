import { NavLink } from 'react-router-dom';

const NAV = [
  { to: '/',        icon: '⊞',  label: 'Overview'  },
  { to: '/devices', icon: '📡', label: 'Devices'   },
  { to: '/auth',    icon: '👁',  label: 'Auth'      },
  { to: '/users',   icon: '👥', label: 'Users'     },
  { to: '/alerts',  icon: '🚨', label: 'Alerts'    },
  { to: '/voice',   icon: '🎙', label: 'Voice AI'  },
  { to: '/health',  icon: '♥',  label: 'Health'    },
];

export default function Sidebar({ anomalyCount = 0 }) {
  return (
    <aside className="sidebar">
      <div className="sidebar-logo">
        <span className="sidebar-logo-icon">👁</span>
        <span className="sidebar-logo-text">Iris</span>
      </div>

      <nav className="sidebar-nav">
        {NAV.map(({ to, icon, label }) => (
          <NavLink
            key={to}
            to={to}
            end={to === '/'}
            className={({ isActive }) =>
              `sidebar-item${isActive ? ' sidebar-item--active' : ''}`
            }
          >
            <span className="sidebar-icon">{icon}</span>
            <span className="sidebar-label">{label}</span>
            {label === 'Alerts' && anomalyCount > 0 && (
              <span className="sidebar-badge">{anomalyCount}</span>
            )}
            {label === 'Voice AI' && (
              <span className="sidebar-new">AI</span>
            )}
          </NavLink>
        ))}
      </nav>

      <div className="sidebar-footer">
        <span className="sidebar-version">v2.0</span>
      </div>
    </aside>
  );
}
