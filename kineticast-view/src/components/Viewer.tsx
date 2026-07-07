"use client";

import { useEffect, useState, useRef } from 'react';
import { Canvas, useFrame } from '@react-three/fiber';
import { OrbitControls, Grid, Line } from '@react-three/drei';
import * as THREE from 'three';
import { db } from '@/lib/firebase';
import { collection, query, orderBy, limit, onSnapshot } from 'firebase/firestore';

// テレメトリデータの型定義（anyの代わりにこれを使用）
interface TelemetrySample {
  t: number;
  px?: number;
  py?: number;
  pz?: number;
  qx?: number;
  qy?: number;
  qz?: number;
  qw?: number;
  vz?: number;
  [key: string]: number | undefined; // その他の数値データ用
}

interface RocketModelProps {
  currentSample: TelemetrySample | null;
}

// --- 3D空間内のロケット（円錐）を動かすコンポーネント ---
function RocketModel({ currentSample }: RocketModelProps) {
  const meshRef = useRef<THREE.Mesh>(null);

  useFrame(() => {
    if (!meshRef.current || !currentSample) return;

    // 世界座標（Z=高度）を Three.js の座標系（Y=高度）にマッピング
    const x = currentSample.px || 0;
    const y = currentSample.pz || 0; 
    const z = -(currentSample.py || 0);
    meshRef.current.position.set(x, y, z);

    // クォータニオンによる姿勢（傾き）の反映
    if (
      currentSample.qw !== undefined && 
      currentSample.qx !== undefined && 
      currentSample.qy !== undefined && 
      currentSample.qz !== undefined
    ) {
      const q = new THREE.Quaternion(
        currentSample.qx,
        currentSample.qz,  // センサーのZ ➔ Three.jsのY
        -currentSample.qy, // センサーのY ➔ Three.jsの-Z
        currentSample.qw
      );
      meshRef.current.quaternion.copy(q);
    }
  });

  return (
    <mesh ref={meshRef}>
      <coneGeometry args={[0.5, 3, 16]} />
      <meshNormalMaterial />
    </mesh>
  );
}

interface RocketTrajectoryViewerProps {
  flightId: string;
}

// --- メインビュワー ---
export default function RocketTrajectoryViewer({ flightId }: RocketTrajectoryViewerProps) {
  const [currentSample, setCurrentSample] = useState<TelemetrySample | null>(null);
  const [trajectory, setTrajectory] = useState<[number, number, number][]>([[0, 0, 0]]);
  
  const sampleQueueRef = useRef<TelemetrySample[]>([]);
  const trajectoryPointsRef = useRef<[number, number, number][]>([[0, 0, 0]]);
  const animationIntervalRef = useRef<NodeJS.Timeout | null>(null);

  useEffect(() => {
    if (!flightId) return;

    // Firestoreの最新バッチを常時監視
    const telemetryRef = collection(db, 'flights', flightId, 'telemetry');
    const q = query(telemetryRef, orderBy('__name__', 'desc'), limit(1));

    const unsubscribe = onSnapshot(q, (snapshot) => {
      snapshot.docChanges().forEach((change) => {
        if (change.type === 'added' || change.type === 'modified') {
          const data = change.doc.data();
          if (data && Array.isArray(data.samples)) {
            sampleQueueRef.current.push(...(data.samples as TelemetrySample[]));
          }
        }
      });
    });

    // 10ms (100Hz) 再生タイマー
    animationIntervalRef.current = setInterval(() => {
      if (sampleQueueRef.current.length > 0) {
        const nextSample = sampleQueueRef.current.shift();
        if (!nextSample) return;
        
        setCurrentSample(nextSample);

        // 軌跡プロットの追加
        const pX = nextSample.px || 0;
        const pY = nextSample.pz || 0; 
        const pZ = -(nextSample.py || 0);
        
        trajectoryPointsRef.current.push([pX, pY, pZ]);
        
        // 描画が重くならないよう5サンプル毎に線を更新
        if (sampleQueueRef.current.length % 5 === 0 || sampleQueueRef.current.length === 0) {
          setTrajectory([...trajectoryPointsRef.current]);
        }
      }
    }, 10);

    return () => {
      unsubscribe();
      if (animationIntervalRef.current) clearInterval(animationIntervalRef.current);
    };
  }, [flightId]);

  return (
    <div style={{ width: '100vw', height: '100vh', position: 'relative', background: '#1a1a1a' }}>
      {/* 画面左上のステータスHUD */}
      <div style={{ position: 'absolute', top: 20, left: 20, color: '#fff', zIndex: 10, fontFamily: 'monospace', background: 'rgba(0,0,0,0.7)', padding: '15px', borderRadius: '8px' }}>
        <h2 style={{ margin: '0 0 10px 0', color: '#00ffcc' }}>🚀 KinetiCast 3D Viewer</h2>
        <p>Flight ID: {flightId}</p>
        {currentSample ? (
          <>
            <p>高度 (Z): <span style={{ color: '#ffcc00', fontWeight: 'bold' }}>{currentSample.pz?.toFixed(2)} m</span></p>
            <p>水平 (X, Y): ({currentSample.px?.toFixed(1)}, {currentSample.py?.toFixed(1)}) m</p>
            <p>速度 (Vz): {currentSample.vz?.toFixed(1)} m/s</p>
          </>
        ) : (
          <p>データ受信待機中...</p>
        )}
      </div>

      <Canvas camera={{ position: [10, 15, 30], fov: 60 }}>
        <ambientLight intensity={0.6} />
        <directionalLight position={[10, 20, 10]} intensity={0.8} />
        
        {/* 地面の無限グリッド */}
        <Grid cellSize={1} cellThickness={0.5} sectionSize={10} sectionColor="#444" cellColor="#222" position={[0, 0, 0]} infiniteGrid />
        
        {/* 飛行軌跡の赤いライン */}
        {trajectory.length > 1 && <Line points={trajectory} color="#ff3333" lineWidth={3} />}
        
        {/* ロケット本体 */}
        <RocketModel currentSample={currentSample} />
        
        {/* マウス視点操作 */}
        <OrbitControls enableDamping dampingFactor={0.05} />
      </Canvas>
    </div>
  );
}