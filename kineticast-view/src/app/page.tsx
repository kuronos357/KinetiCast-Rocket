"use client";

import { useState } from 'react';
import RocketTrajectoryViewer from '@/components/RocketTrajectoryViewer';

export default function Home() {
  const [inputId, setInputId] = useState('');
  const [activeFlightId, setActiveFlightId] = useState('');

  if (activeFlightId) {
    return <RocketTrajectoryViewer flightId={activeFlightId} />;
  }

  return (
    <main style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100vh', background: '#111', color: '#fff', fontFamily: 'sans-serif' }}>
      <div style={{ background: '#222', padding: '40px', borderRadius: '12px', boxShadow: '0 4px 20px rgba(0,0,0,0.5)', textAlign: 'center' }}>
        <h1 style={{ color: '#00ffcc', marginBottom: '20px' }}>🚀 KinetiCast Control Panel</h1>
        <p style={{ color: '#aaa', marginBottom: '30px' }}>test-senderが生成したフライトIDを入力してください</p>
        
        <div style={{ display: 'flex', gap: '10px', justifyContent: 'center' }}>
          <input
            type="text"
            placeholder="例: 1718956800"
            value={inputId}
            onChange={(e) => setInputId(e.target.value)}
            style={{ padding: '12px 20px', borderRadius: '6px', border: '1px solid #444', background: '#333', color: '#fff', fontSize: '16px', width: '200px' }}
          />
          <button
            onClick={() => setActiveFlightId(inputId)}
            style={{ padding: '12px 24px', borderRadius: '6px', border: 'none', background: '#00ffcc', color: '#000', fontWeight: 'bold', fontSize: '16px', cursor: 'pointer' }}
          >
            3D監視を開始
          </button>
        </div>
      </div>
    </main>
  );
}