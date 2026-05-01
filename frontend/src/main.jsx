import React from 'react';
import ReactDOM from 'react-dom/client';
import {
  Chart, LineElement, PointElement, LinearScale,
  CategoryScale, Tooltip, Legend, Filler,
} from 'chart.js';
import { ToastProvider } from './components/ToastContainer.jsx';
import App from './App.jsx';
import './styles.css';

Chart.register(LineElement, PointElement, LinearScale, CategoryScale, Tooltip, Legend, Filler);

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <ToastProvider>
      <App />
    </ToastProvider>
  </React.StrictMode>
);
