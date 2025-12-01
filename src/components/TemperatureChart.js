import React from 'react';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  TimeScale,
} from 'chart.js';
import { Line } from 'react-chartjs-2';
import 'chartjs-adapter-date-fns';

// Register ChartJS components
ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Title,
  Tooltip,
  Legend,
  TimeScale
);

const TemperatureChart = ({ temperatureData = [], isRecording = false, startTime = null }) => {
  // Validasi data
  if (!temperatureData || temperatureData.length === 0) {
    return (
      <div className="chart-container">
        <div className="chart-placeholder">
          <p>📊 Tidak ada data grafik</p>
          <p>Klik "Start Recording" untuk memulai monitoring</p>
        </div>
      </div>
    );
  }

  // Options untuk chart dengan timeline dinamis
  const options = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
      legend: {
        position: 'top',
        labels: {
          usePointStyle: true,
          padding: 20
        }
      },
      title: {
        display: true,
        text: isRecording ? 'Grafik Monitoring Suhu (Live Recording)' : 'Grafik Monitoring Suhu',
        font: {
          size: 16,
          weight: 'bold'
        },
        padding: 20
      },
      tooltip: {
        mode: 'index',
        intersect: false,
        callbacks: {
          label: function (context) {
            let label = context.dataset.label || '';
            if (label) {
              label += ': ';
            }
            if (context.parsed.y !== null) {
              label += `${context.parsed.y}°C`;
            }
            return label;
          },
          title: function (tooltipItems) {
            const dataIndex = tooltipItems[0].dataIndex;
            const timestamp = temperatureData[dataIndex].x;
            const date = new Date(timestamp);
            return date.toLocaleString('id-ID', {
              hour: '2-digit',
              minute: '2-digit',
              second: '2-digit'
            });
          }
        }
      }
    },
    scales: {
      x: {
        type: 'time',
        time: {
          unit: 'minute',
          stepSize: 1,
          displayFormats: {
            minute: 'HH:mm',
            hour: 'HH:mm'
          },
          tooltipFormat: 'HH:mm:ss'
        },
        title: {
          display: true,
          text: 'Waktu'
        },
        grid: {
          color: 'rgba(0, 0, 0, 0.1)'
        },
        // Timeline dinamis - mulai dari start time
        min: startTime ? startTime.getTime() : (temperatureData.length > 0 ? temperatureData[0].x : undefined),
        // max dihapus agar chart hanya memanjang saat ada data baru (tidak jitter setiap detik)
      },
      y: {
        min: 30,
        max: 70,
        title: {
          display: true,
          text: 'Suhu (°C)'
        },
        grid: {
          color: 'rgba(0, 0, 0, 0.1)'
        },
        ticks: {
          callback: function (value) {
            return value + '°C';
          }
        }
      },
    },
    interaction: {
      intersect: false,
      mode: 'nearest'
    },
    animation: {
      duration: 1000
    }
  };

  // Data untuk chart
  const data = {
    datasets: [
      {
        label: 'Suhu Kompos',
        data: temperatureData,
        borderColor: 'rgb(255, 99, 132)',
        backgroundColor: 'rgba(255, 99, 132, 0.1)',
        borderWidth: 3,
        fill: true,
        tension: 0,
        pointBackgroundColor: 'rgb(255, 99, 132)',
        pointBorderColor: '#fff',
        pointBorderWidth: 2,
        pointRadius: 4,
        pointHoverRadius: 7,
        spanGaps: true
      },
      {
        label: 'Rentang Optimal (40-65°C)',
        data: temperatureData.map(item => ({
          x: item.x,
          y: 40
        })),
        borderColor: 'rgba(75, 192, 192, 0.6)',
        backgroundColor: 'rgba(75, 192, 192, 0.05)',
        borderWidth: 2,
        borderDash: [5, 5],
        fill: false,
        pointRadius: 0
      },
      {
        label: 'Batas Maksimal',
        data: temperatureData.map(item => ({
          x: item.x,
          y: 65
        })),
        borderColor: 'rgba(255, 159, 64, 0.6)',
        backgroundColor: 'rgba(255, 159, 64, 0.05)',
        borderWidth: 2,
        borderDash: [5, 5],
        fill: false,
        pointRadius: 0
      }
    ],
  };

  return (
    <div className="chart-container">
      <div className="chart-wrapper">
        <Line options={options} data={data} />
      </div>

      <div className="chart-legend">
        <div className="legend-item">
          <span className="legend-color" style={{ backgroundColor: 'rgb(255, 99, 132)' }}></span>
          <span>Suhu Aktual Kompos</span>
        </div>
        <div className="legend-item">
          <span className="legend-color" style={{ backgroundColor: 'rgba(75, 192, 192, 0.6)' }}></span>
          <span>Batas Minimal Optimal (40°C)</span>
        </div>
        <div className="legend-item">
          <span className="legend-color" style={{ backgroundColor: 'rgba(255, 159, 64, 0.6)' }}></span>
          <span>Batas Maksimal Optimal (65°C)</span>
        </div>
      </div>

      {isRecording && (
        <div className="live-indicator">
          <div className="pulse"></div>
          LIVE - Grafik sedang merekam...
        </div>
      )}
    </div>
  );
};

export default TemperatureChart;