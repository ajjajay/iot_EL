import EnrolledUsers from '../components/EnrolledUsers.jsx';
import EnrollPanel   from '../components/EnrollPanel.jsx';

export default function UsersPage({ devices, users, onRemove, onSendEnroll, onWebcamEnroll, onUpdateUser }) {
  return (
    <>
      <div className="page-header">
        <h2 className="page-heading">Enrolled Users</h2>
        <p className="page-sub">Manage iris templates, roles, and device access</p>
      </div>
      <EnrolledUsers users={users} devices={devices} onRemove={onRemove} onUpdateUser={onUpdateUser} />
      <EnrollPanel
        devices={devices}
        onSendEnroll={onSendEnroll}
        onWebcamEnroll={onWebcamEnroll}
      />
    </>
  );
}
