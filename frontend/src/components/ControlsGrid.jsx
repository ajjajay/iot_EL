export default function ControlsGrid({ devices, onRelayCommand }) {
  const entries = Object.entries(devices);

  return (
    <section className="section">
      <h2 className="section-title">Manual Door Controls</h2>
      <p className="section-subtitle">Relay overrides expire automatically after 5 minutes.</p>
      <div className="controls-grid" id="controlsGrid">
        {entries.map(([id, device]) => {
          const isOn = device.commands?.relayOverride === 'ON';
          return (
            <div key={id} className="control-card">
              <div className="control-card-title">{id}</div>
              <div className="control-card-sub">{device.meta?.location ?? ''}</div>
              <div className="toggle-row">
                <span className="toggle-label">Door Relay / Lock</span>
                <label className="toggle" title="Manual override — auto-expires in 5 min">
                  <input
                    type="checkbox"
                    checked={isOn}
                    onChange={e => onRelayCommand(id, e.target.checked ? 'ON' : 'OFF')}
                  />
                  <span className="toggle-slider"></span>
                </label>
              </div>
            </div>
          );
        })}
      </div>
    </section>
  );
}
