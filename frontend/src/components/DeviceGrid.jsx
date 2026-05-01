import DeviceCard from './DeviceCard.jsx';

export default function DeviceGrid({ devices, signins }) {
  const entries = Object.entries(devices);

  return (
    <section className="section">
      <h2 className="section-title">Device Status</h2>
      <div className="device-grid" id="deviceGrid">
        {entries.length === 0 ? (
          <>
            <div className="skeleton-card"><div className="skeleton-inner"></div></div>
            <div className="skeleton-card"><div className="skeleton-inner"></div></div>
          </>
        ) : (
          entries.map(([id, device]) => (
            <DeviceCard
              key={id}
              id={id}
              device={device}
              lastSignin={(signins[id] || [])[0] ?? null}
            />
          ))
        )}
      </div>
    </section>
  );
}
