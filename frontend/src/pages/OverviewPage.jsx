import SummaryStrip from '../components/SummaryStrip.jsx';
import DeviceGrid   from '../components/DeviceGrid.jsx';

export default function OverviewPage({ devices, signins, users, alerts, tick }) {
  return (
    <>
      <SummaryStrip key={tick} devices={devices} signins={signins} users={users} alerts={alerts} />
      <DeviceGrid devices={devices} signins={signins} />
    </>
  );
}
