import { initializeApp } from "firebase/app";
import { getAnalytics } from "firebase/analytics";
import { getDatabase } from "firebase/database";

// Config untuk development dan production
const firebaseConfig = {
  apiKey: process.env.REACT_APP_FIREBASE_API_KEY || "dummy-api-key-for-development",
  authDomain: process.env.REACT_APP_FIREBASE_AUTH_DOMAIN || "dummy-auth-domain",
  databaseURL: process.env.REACT_APP_FIREBASE_DATABASE_URL || "https://dummy-database.firebaseio.com",
  projectId: process.env.REACT_APP_FIREBASE_PROJECT_ID || "dummy-project-id",
  storageBucket: process.env.REACT_APP_FIREBASE_STORAGE_BUCKET || "dummy-storage-bucket",
  messagingSenderId: process.env.REACT_APP_FIREBASE_MESSAGING_SENDER_ID || "123456789",
  appId: process.env.REACT_APP_FIREBASE_APP_ID || "dummy-app-id",
  measurementId: process.env.REACT_APP_FIREBASE_MEASUREMENT_ID || "G-DUMMYID"
};

// Cek jika menggunakan dummy values (development)
const isUsingDummyConfig = firebaseConfig.apiKey === "dummy-api-key-for-development";

if (isUsingDummyConfig) {
  console.warn('⚠️  Using dummy Firebase config. For production, set environment variables.');
}

const app = initializeApp(firebaseConfig);
const analytics = getAnalytics(app);
const database = getDatabase(app);

export { app, analytics, database };