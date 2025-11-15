import React, { useState, useEffect } from 'react';
import SensorCard from './SensorCard';
import { 
  Thermometer, 
  Droplets, 
  Gauge, 
  Wind, 
  Zap, 
  Leaf 
} from 'lucide-react';

// Import dengan dynamic import untuk menghindari circular dependency
let database;
const initializeFirebase = async () => {
  const firebaseModule = await import('../firebase/config.prod');
  database = firebaseModule.database;
};

const Dashboard = () => {
  const [sensorData, setSensorData] = useState({
    temperature: {
      value: null,
      unit: '°C',
      status: 'loading',
      min: 0,
      max: 80,
      optimalRange: { min: 40, max: 65 },
      description: 'Suhu optimal untuk aktivitas mikroba thermophilic'
    },
    moisture: {
      value: null,
      unit: '%',
      status: 'loading',
      min: 0,
      max: 100,
      optimalRange: { min: 40, max: 60 },
      description: 'Kelembaban ideal untuk proses pengomposan'
    },
    ph: {
      value: null,
      unit: 'pH',
      status: 'loading',
      min: 0,
      max: 14,
      optimalRange: { min: 6.5, max: 8.0 },
      description: 'Tingkat keasaman netral yang ideal'
    },
    gas: {
      value: null,
      unit: 'ppm',
      status: 'loading',
      min: 0,
      max: 500,
      optimalRange: { min: 0, max: 200 },
      description: 'Level gas amonia dalam batas normal'
    },
    ec: {
      value: null,
      unit: 'mS/cm',
      status: 'loading',
      min: 0,
      max: 5,
      optimalRange: { min: 1, max: 2 },
      description: 'Konduktivitas listrik agak tinggi'
    },
    maturity: {
      value: null,
      unit: '%',
      status: 'loading',
      min: 0,
      max: 100,
      optimalRange: { min: 80, max: 100 },
      description: 'Kompos sedang dalam proses pematangan'
    }
  });

  const [lastUpdate, setLastUpdate] = useState(new Date());
  const [firebaseInitialized, setFirebaseInitialized] = useState(false);

  // Fungsi untuk menentukan status berdasarkan nilai
  const getStatus = (value, optimalMin, optimalMax) => {
    if (value === null || value === undefined) return 'loading';
    if (value >= optimalMin && value <= optimalMax) return 'optimal';
    if (value < optimalMin - 5 || value > optimalMax + 5) return 'warning';
    return 'attention';
  };

  // Inisialisasi Firebase
  useEffect(() => {
    initializeFirebase().then(() => {
      setFirebaseInitialized(true);
      console.log('Firebase siap digunakan');
    }).catch(error => {
      console.error('Gagal inisialisasi Firebase:', error);
    });
  }, []);

  // Read data dari Firebase Realtime Database
  useEffect(() => {
    if (!firebaseInitialized || !database) {
      console.log('Menunggu inisialisasi Firebase...');
      return;
    }

    console.log('Mencoba connect ke Firebase...');
    
    const { ref, onValue } = require('firebase/database');
    const sensorRef = ref(database, 'sensors');
    
    const unsubscribe = onValue(sensorRef, (snapshot) => {
      const data = snapshot.val();
      console.log('Data dari Firebase:', data);
      
      if (data) {
        setSensorData(prevData => ({
          temperature: {
            ...prevData.temperature,
            value: data.temperature !== undefined ? data.temperature : null,
            status: getStatus(data.temperature, 40, 65)
          },
          moisture: {
            ...prevData.moisture,
            value: data.moisture !== undefined ? data.moisture : null,
            status: getStatus(data.moisture, 40, 60)
          },
          ph: {
            ...prevData.ph,
            value: data.ph !== undefined ? data.ph : null,
            status: getStatus(data.ph, 6.5, 8.0)
          },
          gas: {
            ...prevData.gas,
            value: data.gas !== undefined ? data.gas : null,
            status: getStatus(data.gas, 0, 200)
          },
          ec: {
            ...prevData.ec,
            value: data.ec !== undefined ? data.ec : null,
            status: getStatus(data.ec, 1, 2)
          },
          maturity: {
            ...prevData.maturity,
            value: data.maturity !== undefined ? data.maturity : null,
            status: getStatus(data.maturity, 80, 100)
          }
        }));
        
        setLastUpdate(new Date());
      } else {
        console.log('Tidak ada data di Firebase');
        // Set default data untuk testing
        setSensorData(prevData => ({
          temperature: { ...prevData.temperature, value: 55.2, status: 'optimal' },
          moisture: { ...prevData.moisture, value: 52, status: 'optimal' },
          ph: { ...prevData.ph, value: 7.2, status: 'optimal' },
          gas: { ...prevData.gas, value: 125, status: 'optimal' },
          ec: { ...prevData.ec, value: 2.1, status: 'warning' },
          maturity: { ...prevData.maturity, value: 75, status: 'attention' }
        }));
      }
    }, (error) => {
      console.error('Error reading from Firebase:', error);
    });

    // Cleanup subscription
    return () => {
      if (unsubscribe) unsubscribe();
    };
  }, [firebaseInitialized]);

  return (
    <div>
      <div className="header">
        <h1>🌱 Kompos Monitoring System</h1>
        <p>Monitor kondisi kompos Anda secara real-time</p>
        {!firebaseInitialized && (
          <div style={{ color: 'yellow', marginTop: '10px' }}>
            Menghubungkan ke database...
          </div>
        )}
      </div>

      <div className="dashboard">
        <SensorCard
          title="Suhu"
          value={sensorData.temperature.value}
          unit={sensorData.temperature.unit}
          status={sensorData.temperature.status}
          min={sensorData.temperature.min}
          max={sensorData.temperature.max}
          optimalRange={sensorData.temperature.optimalRange}
          description={sensorData.temperature.description}
          icon={<Thermometer size={24} />}
          type="temperature"
        />

        <SensorCard
          title="Kelembaban"
          value={sensorData.moisture.value}
          unit={sensorData.moisture.unit}
          status={sensorData.moisture.status}
          min={sensorData.moisture.min}
          max={sensorData.moisture.max}
          optimalRange={sensorData.moisture.optimalRange}
          description={sensorData.moisture.description}
          icon={<Droplets size={24} />}
          type="moisture"
        />

        <SensorCard
          title="pH Level"
          value={sensorData.ph.value}
          unit={sensorData.ph.unit}
          status={sensorData.ph.status}
          min={sensorData.ph.min}
          max={sensorData.ph.max}
          optimalRange={sensorData.ph.optimalRange}
          description={sensorData.ph.description}
          icon={<Gauge size={24} />}
          type="ph"
        />

        <SensorCard
          title="Gas Amonia"
          value={sensorData.gas.value}
          unit={sensorData.gas.unit}
          status={sensorData.gas.status}
          min={sensorData.gas.min}
          max={sensorData.gas.max}
          optimalRange={sensorData.gas.optimalRange}
          description={sensorData.gas.description}
          icon={<Wind size={24} />}
          type="gas"
        />

        <SensorCard
          title="Konduktivitas"
          value={sensorData.ec.value}
          unit={sensorData.ec.unit}
          status={sensorData.ec.status}
          min={sensorData.ec.min}
          max={sensorData.ec.max}
          optimalRange={sensorData.ec.optimalRange}
          description={sensorData.ec.description}
          icon={<Zap size={24} />}
          type="ec"
        />

        <SensorCard
          title="Kematangan"
          value={sensorData.maturity.value}
          unit={sensorData.maturity.unit}
          status={sensorData.maturity.status}
          min={sensorData.maturity.min}
          max={sensorData.maturity.max}
          optimalRange={sensorData.maturity.optimalRange}
          description={sensorData.maturity.description}
          icon={<Leaf size={24} />}
          type="maturity"
        />
      </div>

      <div className="last-update">
        Terakhir diperbarui: {lastUpdate.toLocaleTimeString('id-ID')}
        {!firebaseInitialized && ' (Offline Mode)'}
      </div>
    </div>
  );
};

export default Dashboard;