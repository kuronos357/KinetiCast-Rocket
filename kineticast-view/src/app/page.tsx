"use client";

import { useState, useEffect } from 'react';
import RocketTrajectoryViewer from '@/components/Viewer';
import { db } from '@/lib/firebase';
import { collection, getDocs } from 'firebase/firestore';

export default function Home() {
  const [flightIds, setFlightIds] = useState<string[]>([]);
  const [selectedId, setSelectedId] = useState('');
  const [activeFlightId, setActiveFlightId] = useState('');
  const [loading, setLoading] = useState(true);

  // 起動時にFirestoreから存在するフライトID（履歴）をすべて取得する
  useEffect(() => {
    const fetchFlights = async () => {
      try {
        const querySnapshot = await getDocs(collection(db, 'flights'));
        const ids: string[] = [];
        querySnapshot.forEach((doc) => {
          ids.push(doc.id);
        });
        // 新しいフライト（数字が大きいもの）が上に来るようにソート
        ids.sort((a, b) => b.localeCompare(a));
        setFlightIds(ids);
        if (ids.length > 0) setSelectedId(ids[0]); // 初期値を最新のIDにする
      } catch (error) {
        console.error("フライト一覧の取得に失敗:", error);
      } finally {
        setLoading(false);
      }
    };
    fetchFlights();
  }, []);

  if (activeFlightId) {
    return <RocketTrajectoryViewer flightId={activeFlightId} />;
  }

  return (
    <main style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100vh', background: '#111', color: '#fff', fontFamily: 'sans-serif' }}>
      <div style={{ background: '#222', padding: '40px', borderRadius: '12px', boxShadow: '0 4px 20px rgba(0,0,0,0.5)', textAlign: 'center', minWidth: '400px' }}>
        <h1 style={{ color: '#00ffcc', marginBottom: '20px' }}>🚀 KinetiCast Control Panel</h1>
        
        {loading ? (
          <p style={{ color: '#aaa' }}>フライト履歴を読み込み中...</p>
        ) : flightIds.length === 0 ? (
          <p style={{ color: '#ff3333' }}>フライトデータがFirestoreに見つかりません。test-senderを動かしてください。</p>
        ) : (
          <>
            <p style={{ color: '#aaa', marginBottom: '30px' }}>監視またはリプレイするフライトを選択してください</p>
            
            <div style={{ display: 'flex', flexDirection: 'column', gap: '20px', alignItems: 'center' }}>
              <select
                value={selectedId}
                onChange={(e) => setSelectedId(e.target.value)}
                style={{ padding: '12px 20px', borderRadius: '6px', border: '1px solid #444', background: '#333', color: '#fff', fontSize: '16px', width: '100%', cursor: 'pointer' }}
              >
                {flightIds.map((id) => {
                  // タイムスタンプ（秒）の場合は読みやすい日時に変換する（おまけ機能）
                  const isTimestamp = /^\d+$/.test(id);
                  const label = isTimestamp ? `${new Date(parseInt(id) * 1000).toLocaleString()} (${id})` : id;
                  return <option key={id} value={id}>{label}</option>;
                })}
              </select>

              <button
                onClick={() => setActiveFlightId(selectedId)}
                style={{ padding: '12px 24px', borderRadius: '6px', border: 'none', background: '#00ffcc', color: '#000', fontWeight: 'bold', fontSize: '16px', cursor: 'pointer', width: '100%' }}
              >
                3D表示（リアルタイム監視 / リプレイ）
              </button>
            </div>
          </>
        )}
      </div>
    </main>
  );
}