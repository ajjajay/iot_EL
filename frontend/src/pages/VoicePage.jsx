import { useState, useRef } from 'react';

export default function VoicePage({ devices, users, alerts }) {
  const [status,     setStatus]     = useState('idle'); // idle | loading | speaking
  const [transcript, setTranscript] = useState('');
  const [timestamp,  setTimestamp]  = useState('');
  const [error,      setError]      = useState('');
  const utteranceRef = useRef(null);

  const deviceList    = Object.values(devices || {});
  const onlineCount   = deviceList.filter(d => d.online).length;
  const userCount     = Object.keys(users || {}).length;
  const alertCount    = (alerts || []).length;

  const handleSpeak = async () => {
    if (status === 'speaking') {
      window.speechSynthesis.cancel();
      setStatus('idle');
      return;
    }

    setStatus('loading');
    setError('');

    try {
      const res = await fetch('/api/voice-update', { method: 'POST' });
      const body = await res.json();
      if (!res.ok) throw new Error(body.detail || `Server error ${res.status}`);

      setTranscript(body.text);
      setTimestamp(body.timestamp);
      setStatus('speaking');

      const utterance      = new SpeechSynthesisUtterance(body.text);
      utterance.rate       = 0.92;
      utterance.pitch      = 1.0;
      utterance.onend      = () => setStatus('idle');
      utterance.onerror    = () => setStatus('idle');
      utteranceRef.current = utterance;
      window.speechSynthesis.speak(utterance);
    } catch (err) {
      setError(err.message);
      setStatus('idle');
    }
  };

  const orbClass =
    status === 'loading'  ? 'voice-orb voice-orb--loading'  :
    status === 'speaking' ? 'voice-orb voice-orb--speaking'  :
                            'voice-orb voice-orb--idle';

  const hint =
    status === 'idle'     ? 'Click to get a live spoken briefing from Claude' :
    status === 'loading'  ? 'Fetching live data and generating briefing…'      :
                            'Speaking — click to stop';

  return (
    <div className="voice-page">

      {/* Hero */}
      <div className="voice-hero">
        <p className="voice-hero-eyebrow">Powered by Claude Haiku</p>
        <h1 className="voice-hero-title">AI Security Briefing</h1>
        <p className="voice-hero-sub">
          One click for a spoken summary of your entire security system
        </p>

        {/* Orb */}
        <div className="voice-orb-wrap">
          <button className={orbClass} onClick={handleSpeak} aria-label="Voice briefing">
            {status === 'loading'  && <span className="voice-orb-spinner" />}
            {status === 'idle'     && <span className="voice-orb-icon">🎙</span>}
            {status === 'speaking' && (
              <div className="voice-orb-wave">
                <span /><span /><span /><span /><span />
              </div>
            )}
          </button>
        </div>

        <p className="voice-orb-hint">{hint}</p>
      </div>

      {/* Error */}
      {error && (
        <div className="voice-error-banner">
          <strong>Error:</strong> {error}
        </div>
      )}

      {/* Transcript */}
      {transcript && (
        <div className="voice-transcript-card">
          <div className="voice-tc-header">
            <span className="voice-tc-title">Latest Briefing</span>
            {timestamp && <span className="voice-tc-ts">{timestamp}</span>}
            {status === 'speaking' && (
              <div className="voice-inline-wave">
                <span /><span /><span /><span /><span />
              </div>
            )}
          </div>
          <p className="voice-tc-text">{transcript}</p>
          <button
            className="voice-tc-replay"
            onClick={handleSpeak}
            disabled={status === 'loading'}
          >
            {status === 'speaking' ? '⏹ Stop' : '▶ Speak Again'}
          </button>
        </div>
      )}

      {/* Quick stats */}
      <div className="voice-stats-row">
        <div className="voice-stat">
          <span className="voice-stat-val">{onlineCount}/{deviceList.length}</span>
          <span className="voice-stat-lbl">Devices Online</span>
        </div>
        <div className="voice-stat">
          <span className="voice-stat-val">{userCount}</span>
          <span className="voice-stat-lbl">Enrolled Users</span>
        </div>
        <div className={`voice-stat${alertCount > 0 ? ' voice-stat--alert' : ''}`}>
          <span className="voice-stat-val">{alertCount}</span>
          <span className="voice-stat-lbl">Active Alerts</span>
        </div>
      </div>

    </div>
  );
}
