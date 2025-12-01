import React, { useState, useEffect, useRef } from 'react';
import SensorCard from './SensorCard';
import TemperatureChart from './TemperatureChart';
import {
  Thermometer,
  Droplets,
  Gauge,
  Wind,
  Zap,
  Leaf,
  Play,
  Square
} from 'lucide-react';
import { database } from '../firebase/config';
import { ref, onValue } from 'firebase/database';

const SENSOR_CONFIG = {
  temperature: {
    title: 'Suhu',
    unit: '°C',
    min: 0,
    max: 80,
    optimalRange: { min: 40, max: 65 },
    description: 'Suhu optimal untuk aktivitas mikroba thermophilic',
    icon: <Thermometer size={24} />,
    defaultValue: 55.2
  },
  moisture: {
    title: 'Kelembaban',
    unit: '%',
    min: 0,
    max: 100,
    optimalRange: { min: 40, max: 60 },
    description: 'Kelembaban ideal untuk proses pengomposan',
    icon: <Droplets size={24} />,
    defaultValue: 52
  },
  ph: {
    title: 'pH Level',
    unit: 'pH',
    min: 0,
    max: 14,
    optimalRange: { min: 6.5, max: 8.0 },
    description: 'Tingkat keasaman netral yang ideal',
    icon: <Gauge size={24} />,
    defaultValue: 7.2
  },
  gas: {
    title: 'Gas Amonia',
    unit: 'ppm',
    min: 0,
    max: 500,
    optimalRange: { min: 0, max: 200 },
    description: 'Level gas amonia dalam batas normal',
    icon: <Wind size={24} />,
    defaultValue: 125
  },
  ec: {
    title: 'Konduktivitas',
    unit: 'mS/cm',
    min: 0,
    max: 5,
    optimalRange: { min: 1, max: 2 },
    description: 'Konduktivitas listrik agak tinggi',
    icon: <Zap size={24} />,
    defaultValue: 2.1
  },
  maturity: {
    title: 'Kematangan',
    unit: '%',
    min: 0,
    max: 100,
    optimalRange: { min: 80, max: 100 },
    description: 'Kompos sedang dalam proses pematangan',
    icon: <Leaf size={24} />,
    defaultValue: 75
  }
};

const Dashboard = () => {
  const getStatus = (value, optimalMin, optimalMax) => {
    if (value === null || value === undefined) return 'loading';
    if (value >= optimalMin && value <= optimalMax) return 'optimal';
    if (value < optimalMin - 5 || value > optimalMax + 5) return 'warning';
    return 'attention';
  };

  const [sensorData, setSensorData] = useState(() => {
    const initial = {};
    Object.entries(SENSOR_CONFIG).forEach(([key, config]) => {
      initial[key] = {
        value: config.defaultValue,
        status: getStatus(config.defaultValue, config.optimalRange.min, config.optimalRange.max)
      };
    });
    return initial;
  });

  const [temperatureHistory, setTemperatureHistory] = useState([]);
  const [lastUpdate, setLastUpdate] = useState(new Date());
  const [firebaseInitialized, setFirebaseInitialized] = useState(false);
  const [currentFirebaseTemp, setCurrentFirebaseTemp] = useState(55.2);

  // Ref to keep track of latest temp for interval
  const tempRef = useRef(55.2);

  const [isRecording, setIsRecording] = useState(false);
  const [startTime, setStartTime] = useState(null);
  const [recordingDuration, setRecordingDuration] = useState(0);

  const recordingIntervalRef = useRef(null);
  const dataCollectionIntervalRef = useRef(null);

  // Update tempRef when currentFirebaseTemp changes
  useEffect(() => {
    tempRef.current = currentFirebaseTemp;
  }, [currentFirebaseTemp]);

  useEffect(() => {
    if (!database) {
      console.log('Menunggu inisialisasi Firebase...');
      return;
    }

    try {
      console.log('Mencoba connect ke Firebase untuk data real...');
      const sensorRef = ref(database, 'sensors');

      const unsubscribe = onValue(sensorRef, (snapshot) => {
        try {
          const data = snapshot.val();
          console.log('Data REAL dari Firebase:', data);

          if (data) {
            const firebaseTemperature = data.temperature !== undefined ? data.temperature : 55.2;
            setCurrentFirebaseTemp(firebaseTemperature);

            setSensorData(prevData => {
              const newData = { ...prevData };
              Object.keys(SENSOR_CONFIG).forEach(key => {
                const value = data[key] !== undefined ? data[key] : prevData[key].value;
                const config = SENSOR_CONFIG[key];
                newData[key] = {
                  value,
                  status: getStatus(value, config.optimalRange.min, config.optimalRange.max)
                };
              });
              return newData;
            });

            setLastUpdate(new Date());
            setFirebaseInitialized(true);
          }
        } catch (error) {
          console.error('Error processing Firebase data:', error);
        }
      }, (error) => {
        console.error('Error reading from Firebase:', error);
      });

      return () => {
        if (unsubscribe) unsubscribe();
      };
    } catch (error) {
      console.error('Error in Firebase setup:', error);
    }
  }, []);

  const addNewDataPoint = () => {
    const currentTemp = tempRef.current;
    if (currentTemp === null) {
      console.log('❌ Tidak bisa tambah data: Data Firebase null');
      return;
    }

    const now = new Date();
    const newDataPoint = {
      x: now.getTime(),
      y: currentTemp
    };

    setTemperatureHistory(prev => {
      const newHistory = [...prev, newDataPoint];
      console.log(`📊 Data point ke-${newHistory.length} ditambahkan:`, {
        time: now.toLocaleTimeString('id-ID'),
        temperature: currentTemp
      });
      return newHistory;
    });

    setLastUpdate(now);
  };

  // Effect untuk mencatat data saat ada PERUBAHAN suhu (Real-time)
  const prevTempRef = useRef(currentFirebaseTemp);
  useEffect(() => {
    if (isRecording) {
      if (currentFirebaseTemp !== prevTempRef.current) {
        console.log('🌡️ Suhu berubah, mencatat data point baru...');
        addNewDataPoint();
        prevTempRef.current = currentFirebaseTemp;
      }
    } else {
      prevTempRef.current = currentFirebaseTemp;
    }
  }, [currentFirebaseTemp, isRecording]);

  useEffect(() => {
    if (!isRecording) return;

    console.log('🔄 Memulai auto data collection setiap 1 menit...');

    dataCollectionIntervalRef.current = setInterval(() => {
      addNewDataPoint();
    }, 60000); // 1 menit = 60000 ms

    return () => {
      if (dataCollectionIntervalRef.current) {
        clearInterval(dataCollectionIntervalRef.current);
        dataCollectionIntervalRef.current = null;
      }
    };
  }, [isRecording]);

  const startRecording = () => {
    if (currentFirebaseTemp === null) {
      alert('Tunggu hingga data Firebase tersedia!');
      return;
    }

    const now = new Date();
    setStartTime(now);
    setIsRecording(true);
    setRecordingDuration(0);

    const initialDataPoint = {
      x: now.getTime(),
      y: currentFirebaseTemp
    };

    setTemperatureHistory([initialDataPoint]);
    setLastUpdate(now);

    console.log(`� Recording dimulai pada: ${now.toLocaleTimeString('id-ID')}`);

    recordingIntervalRef.current = setInterval(() => {
      setRecordingDuration(Math.floor((Date.now() - now.getTime()) / 1000));
    }, 1000);
  };

  const stopRecording = () => {
    setIsRecording(false);

    if (recordingIntervalRef.current) {
      clearInterval(recordingIntervalRef.current);
      recordingIntervalRef.current = null;
    }

    if (dataCollectionIntervalRef.current) {
      clearInterval(dataCollectionIntervalRef.current);
      dataCollectionIntervalRef.current = null;
    }

    console.log(`🔴 Recording dihentikan. Total durasi: ${formatDuration(recordingDuration)}`);
    console.log('Total data points:', temperatureHistory.length);
  };

  const formatDuration = (seconds) => {
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
  };

  const resetChart = () => {
    setTemperatureHistory([]);
    setStartTime(null);
    setRecordingDuration(0);
    console.log('🔄 Grafik direset');
  };

  return (
    <div>
      <div className="header">
        <h1>🌱 Kompos Monitoring System</h1>
        <p>Monitor kondisi kompos Anda secara real-time</p>
        {!firebaseInitialized && (
          <div className="connection-status loading">
            🔄 Menghubungkan ke database Firebase...
          </div>
        )}
        {firebaseInitialized && currentFirebaseTemp && (
          <div className="connection-status connected">
            ✅ Terhubung ke Firebase • Suhu: {currentFirebaseTemp}°C
          </div>
        )}
      </div>

      <div className="chart-section">
        <div className="chart-header">
          <div className="section-title">
            <Thermometer size={28} />
            <h2>Grafik Monitoring Suhu</h2>
          </div>

          <div className="chart-controls">
            {!isRecording ? (
              <button
                className="control-btn start-btn"
                onClick={startRecording}
                disabled={currentFirebaseTemp === null}
              >
                <Play size={16} />
                Start Recording
              </button>
            ) : (
              <button
                className="control-btn stop-btn"
                onClick={stopRecording}
              >
                <Square size={16} />
                Stop Recording
              </button>
            )}

            <button
              className="control-btn reset-btn"
              onClick={resetChart}
              disabled={isRecording}
            >
              Reset Grafik
            </button>

            {isRecording && startTime && (
              <div className="recording-info">
                <div className="recording-indicator">
                  <div className="pulse"></div>
                  REC
                </div>
                <span>Dimulai: {startTime.toLocaleTimeString('id-ID')}</span>
                <span>Durasi: {formatDuration(recordingDuration)}</span>
                <span>Data Points: {temperatureHistory.length}</span>
                <span>Interval: 1 menit</span>
              </div>
            )}
          </div>
        </div>

        <TemperatureChart
          temperatureData={temperatureHistory}
          isRecording={isRecording}
          startTime={startTime}
        />

        {temperatureHistory.length === 0 && !isRecording && (
          <div className="chart-placeholder-instruction">
            <p>🎯 Klik "Start Recording" untuk memulai monitoring grafik suhu</p>
            <p>📊 Data akan diambil setiap menit secara otomatis</p>
          </div>
        )}

        {isRecording && temperatureHistory.length > 0 && (
          <div className="recording-progress">
            <p>⏰ Data point berikutnya: {new Date(lastUpdate.getTime() + 60000).toLocaleTimeString('id-ID')}</p>
            <p>📈 Grafik akan melebar secara otomatis</p>
          </div>
        )}
      </div>

      <div className="dashboard">
        {Object.entries(SENSOR_CONFIG).map(([key, config]) => (
          <SensorCard
            key={key}
            title={config.title}
            value={sensorData[key].value}
            unit={config.unit}
            status={sensorData[key].status}
            min={config.min}
            max={config.max}
            optimalRange={config.optimalRange}
            description={config.description}
            icon={config.icon}
            type={key}
          />
        ))}
      </div>

      <div className="last-update">
        Terakhir diperbarui: {lastUpdate.toLocaleTimeString('id-ID')}
        {firebaseInitialized ? (
          <span> • 🔗 Terhubung ke Firebase</span>
        ) : (
          <span> • ⚠️ Mode offline</span>
        )}
        {isRecording && (
          <span> • 🔴 Recording aktif ({temperatureHistory.length} data points)</span>
        )}
      </div>
    </div>
  );
};

export default Dashboard;