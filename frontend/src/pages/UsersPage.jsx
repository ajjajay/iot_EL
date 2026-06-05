import EnrolledUsers from '../components/EnrolledUsers.jsx';
import EnrollPanel   from '../components/EnrollPanel.jsx';

export default function UsersPage({ devices, users, onRemove, onSendEnroll, onWebcamEnroll }) {
  return (
    <>
      <div className="page-header">
        <h2 className="page-heading">Enrolled Users</h2>
        <p className="page-sub">Manage iris templates and enroll new users</p>
      </div>
      <EnrolledUsers users={users} onRemove={onRemove} />
      <EnrollPanel
        devices={devices}
        onSendEnroll={onSendEnroll}
        onWebcamEnroll={onWebcamEnroll}
      />
    </>
  );
}
