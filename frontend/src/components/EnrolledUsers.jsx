import { useState } from 'react';
import { formatDate } from '../utils/formatters.js';

const ROLE_META = {
  admin:   { label: 'Admin',   cls: 'role-admin'   },
  staff:   { label: 'Staff',   cls: 'role-staff'   },
  visitor: { label: 'Visitor', cls: 'role-visitor'  },
};

function RoleBadge({ role }) {
  const m = ROLE_META[role] ?? { label: role ?? '—', cls: 'role-visitor' };
  return <span className={`role-badge ${m.cls}`}>{m.label}</span>;
}

function DeviceTag({ id }) {
  return <span className="device-tag"><code>{id}</code></span>;
}

function UserCard({ u, deviceIds, onRemove, onUpdateUser }) {
  const [editing,       setEditing]       = useState(false);
  const [editRole,      setEditRole]      = useState(u.role      ?? 'staff');
  const [editDevices,   setEditDevices]   = useState(u.allowedDevices ?? [u.deviceId].filter(Boolean));
  const [saving,        setSaving]        = useState(false);

  function toggleDevice(id) {
    setEditDevices(prev =>
      prev.includes(id) ? prev.filter(d => d !== id) : [...prev, id]
    );
  }

  async function save() {
    setSaving(true);
    await onUpdateUser(u.userId, { role: editRole, allowedDevices: editDevices });
    setSaving(false);
    setEditing(false);
  }

  function cancel() {
    setEditRole(u.role ?? 'staff');
    setEditDevices(u.allowedDevices ?? [u.deviceId].filter(Boolean));
    setEditing(false);
  }

  const allowed = Array.isArray(u.allowedDevices) ? u.allowedDevices : (u.deviceId ? [u.deviceId] : []);

  return (
    <div className={`enrolled-card${u.active === false ? ' enrolled-card--inactive' : ''}${editing ? ' enrolled-card--editing' : ''}`}>
      <div className="enrolled-avatar">
        {u.avatarUrl
          ? <img src={u.avatarUrl} alt={u.name} className="enrolled-avatar-img"
              onError={e => { e.target.style.display = 'none'; e.target.nextSibling.style.display = 'flex'; }} />
          : null}
        <span className="enrolled-avatar-initial" style={u.avatarUrl ? { display: 'none' } : {}}>
          {u.name ? u.name[0].toUpperCase() : '?'}
        </span>
      </div>

      <div className="enrolled-info">
        <div className="enrolled-name">
          {u.name || u.userId}
          <RoleBadge role={u.role} />
          {u.active === false && <span className="enrolled-inactive-badge">Inactive</span>}
        </div>
        <div className="enrolled-id"><code>{u.userId}</code></div>

        {!editing && (
          <div className="enrolled-devices">
            {allowed.length > 0
              ? allowed.map(id => <DeviceTag key={id} id={id} />)
              : <span className="enrolled-meta">No devices assigned</span>}
          </div>
        )}

        {editing && (
          <div className="enrolled-edit-panel">
            <div className="edit-field">
              <label className="edit-label">Role</label>
              <select className="select edit-select" value={editRole} onChange={e => setEditRole(e.target.value)}>
                <option value="admin">Admin — all devices</option>
                <option value="staff">Staff — assigned devices</option>
                <option value="visitor">Visitor — assigned devices</option>
              </select>
            </div>
            <div className="edit-field">
              <label className="edit-label">Allowed devices</label>
              <div className="device-checkboxes">
                {deviceIds.length === 0 && <span className="enrolled-meta">No devices known</span>}
                {deviceIds.map(id => (
                  <label key={id} className="device-checkbox-label">
                    <input
                      type="checkbox"
                      checked={editDevices.includes(id)}
                      onChange={() => toggleDevice(id)}
                      disabled={editRole === 'admin'}
                    />
                    <code>{id}</code>
                  </label>
                ))}
                {editRole === 'admin' && (
                  <span className="edit-hint">Admin bypasses device restrictions</span>
                )}
              </div>
            </div>
            <div className="edit-actions">
              <button className="btn btn-primary btn-sm" onClick={save} disabled={saving}>
                {saving ? 'Saving…' : 'Save'}
              </button>
              <button className="btn btn-ghost btn-sm" onClick={cancel}>Cancel</button>
            </div>
          </div>
        )}
      </div>

      <div className="enrolled-card-actions">
        {!editing && (
          <button className="btn btn-ghost btn-sm" title="Edit role & access" onClick={() => setEditing(true)}>
            ✎
          </button>
        )}
        <button className="btn btn-danger-outline btn-sm enrolled-remove" title="Remove user" onClick={() => onRemove(u.userId)}>
          ✕
        </button>
      </div>
    </div>
  );
}

export default function EnrolledUsers({ users, devices, onRemove, onUpdateUser }) {
  const userList = Object.values(users);
  const deviceIds = Object.keys(devices ?? {});

  return (
    <div className="enrolled-grid" id="enrolledGrid">
      {userList.length === 0 ? (
        <div className="no-alerts">No users enrolled yet</div>
      ) : userList.map(u => (
        <UserCard
          key={u.userId}
          u={u}
          deviceIds={deviceIds}
          onRemove={onRemove}
          onUpdateUser={onUpdateUser}
        />
      ))}
    </div>
  );
}
