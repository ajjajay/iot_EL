import { formatDate } from '../utils/formatters.js';

export default function EnrolledUsers({ users, onRemove }) {
  const userList = Object.values(users);

  return (
    <div className="enrolled-grid" id="enrolledGrid">
      {userList.length === 0 ? (
        <div className="no-alerts">No users enrolled yet</div>
      ) : userList.map(u => {
        const enrollDate = formatDate(u.enrolledAt);
        return (
          <div key={u.userId} className="enrolled-card">
            <div className="enrolled-avatar">
              {u.name ? u.name[0].toUpperCase() : '?'}
            </div>
            <div className="enrolled-info">
              <div className="enrolled-name">{u.name || u.userId}</div>
              <div className="enrolled-id"><code>{u.userId}</code></div>
              <div className="enrolled-meta">
                Device: {u.deviceId || '—'} &nbsp;•&nbsp; Enrolled: {enrollDate}
              </div>
            </div>
            <button
              className="btn btn-danger-outline btn-sm enrolled-remove"
              title="Remove user"
              onClick={() => onRemove(u.userId)}
            >✕</button>
          </div>
        );
      })}
    </div>
  );
}
