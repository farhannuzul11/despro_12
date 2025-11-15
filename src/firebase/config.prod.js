import { initializeApp } from "firebase/app";
import { getAnalytics } from "firebase/analytics";
import { getDatabase } from "firebase/database";

const firebaseConfig = {
  apiKey: "AIzaSyBg8utlpgDx-BZFstI3xTZpX9Tva9WyFhM",
  authDomain: "kompos-8d856.firebaseapp.com",
  databaseURL: "https://kompos-8d856-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "kompos-8d856",
  storageBucket: "kompos-8d856.firebasestorage.app",
  messagingSenderId: "122801497243",
  appId: "1:122801497243:web:ffdf37df38892c3c4a523e",
  measurementId: "G-XT99R1B040"
};

const app = initializeApp(firebaseConfig);
const analytics = getAnalytics(app);
const database = getDatabase(app);

export { app, analytics, database };