"use client";

import { useEffect, useState, useRef } from "react";
import { query, limit, onSnapshot, collection, orderBy } from "firebase/firestore";
import { db } from "@/lib/firebase";
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from "recharts";
import { Gauge, Compass, Image as ImageIcon, Activity } from "lucide-react";
import Image from "next/image";

interface TelemetrySample {
  t: number; ax: number; ay: number; az: number;
  gx: number; gy: number; gz: number;
  mx: number; my: number; mz: number;
  vx: number; vy: number; vz: number;
  px: number; py: number; pz: number;
  heading: number; // 💡 Python側から届く融合済みの高精度絶対方位
}

interface ChartDataPoint {
  time: string; accelZ: number; velocityZ: number; altitude: number; heading: number; px: number; py: number;
}

export default function Viewer() {
  const [currentBatchId, setCurrentBatchId] = useState<string>("Waiting for Session...");
  const [chartData, setChartData] = useState<ChartDataPoint[]>([]);
  const [latestSample, setLatestSample] = useState<TelemetrySample | null>(null);
  const [imageUrl, setImageUrl] = useState<string | null>(null);
  
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const chartDataRef = useRef<ChartDataPoint[]>([]);
  const cameraRef = useRef({ theta: -Math.PI / 4, phi: Math.PI / 6, zoomFactor: 1.0 });

  // ------------------------------------------
  // 📡 二重リアルタイムデータ同期ストリーム
  // ------------------------------------------
  useEffect(() => {
    const sessionQuery = query(
      collection(db, "sessions"),
      orderBy("createdAt", "desc"),
      limit(1)
    );

    let unsubscribeChunks: (() => void) | null = null;

    const unsubscribeSession = onSnapshot(sessionQuery, (sessionSnapshot) => {
      if (!sessionSnapshot.empty) {
        const sessionDoc = sessionSnapshot.docs[0];
        const sessionId = sessionDoc.id;
        const sessionData = sessionDoc.data();
        
        setCurrentBatchId(sessionId);
        setImageUrl(sessionData.imageUrl || null);

        if (unsubscribeChunks) {
          unsubscribeChunks();
          chartDataRef.current = [];
          setChartData([]);
        }

        const chunksQuery = query(
          collection(db, "sessions", sessionId, "chunks"),
          orderBy("timestamp", "asc")
        );

        unsubscribeChunks = onSnapshot(chunksQuery, (chunksSnapshot) => {
          const allSamples: TelemetrySample[] = [];
          
          chunksSnapshot.docs.forEach((chunkDoc) => {
            const chunkData = chunkDoc.data();
            if (chunkData.samples) {
              allSamples.push(...chunkData.samples);
            }
          });

          if (allSamples.length > 0) {
            const last = allSamples[allSamples.length - 1];
            setLatestSample(last);

            const formatted: ChartDataPoint[] = allSamples.map((s) => ({
              time: (s.t / 1000).toFixed(2),
              accelZ: s.az,
              velocityZ: s.vz,
              altitude: s.pz,
              heading: s.heading, // 🧭 生の磁気計算ではなく、Python側が補正したクリーンな値を利用
              px: s.px,
              py: s.py,
            }));
            
            chartDataRef.current = formatted;
            setChartData(formatted);
          }
        });
      }
    });

    return () => {
      unsubscribeSession();
      if (unsubscribeChunks) unsubscribeChunks();
    };
  }, []);

  // ------------------------------------------
  // 🌌 3D Spatial Canvas Rendering Logic
  // ------------------------------------------
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;
    const cx = width / 2;
    const cy = height / 2 + 30;
    const maxRadius = Math.min(cx, cy) - 40;

    const project3D = (x: number, y: number, z: number, maxRange: number) => {
      const { theta, phi, zoomFactor } = cameraRef.current;
      const cosT = Math.cos(theta); const sinT = Math.sin(theta);
      const x1 = x * cosT - y * sinT; const y1 = x * sinT + y * cosT;
      const cosP = Math.cos(phi); const sinP = Math.sin(phi);
      const y2 = y1 * cosP - z * sinP; const z2 = y1 * sinP + z * cosP;
      const scale = (maxRadius / maxRange) * zoomFactor;
      
      return { x: cx + x1 * scale, y: cy - z2 * scale, depth: y2 };
    };

    const draw3DScene = () => {
      ctx.fillStyle = "#030712";
      ctx.fillRect(0, 0, width, height);

      const data = chartDataRef.current;
      let maxRange = 15;
      data.forEach((p) => {
        maxRange = Math.max(maxRange, Math.abs(p.px), Math.abs(p.py), Math.abs(p.altitude));
      });

      // 同心円グリッド
      ctx.lineWidth = 1;
      [0.25, 0.5, 0.75, 1.0].forEach((ratio) => {
        ctx.strokeStyle = "rgba(30, 41, 59, 0.6)";
        ctx.beginPath();
        const r = maxRange * ratio;
        for (let i = 0; i <= 64; i++) {
          const angle = (i / 64) * Math.PI * 2;
          const pt = project3D(Math.cos(angle) * r, Math.sin(angle) * r, 0, maxRange);
          if (i === 0) ctx.moveTo(pt.x, pt.y); else ctx.lineTo(pt.x, pt.y);
        }
        ctx.stroke();
        const lblPt = project3D(r, 0, 0, maxRange);
        ctx.fillStyle = "#475569"; ctx.font = "9px monospace";
        ctx.fillText(`${r.toFixed(0)}m`, lblPt.x + 4, lblPt.y + 3);
      });

      // 垂直高度軸
      ctx.strokeStyle = "rgba(71, 85, 105, 0.3)"; ctx.setLineDash([4, 4]); ctx.beginPath();
      const pBase = project3D(0, 0, 0, maxRange); const pTop = project3D(0, 0, maxRange, maxRange);
      ctx.moveTo(pBase.x, pBase.y); ctx.lineTo(pTop.x, pTop.y); ctx.stroke(); ctx.setLineDash([]);
      [0.5, 1.0].forEach((ratio) => {
        const hPt = project3D(0, 0, maxRange * ratio, maxRange);
        ctx.fillStyle = "#64748b"; ctx.fillText(`Alt:${(maxRange * ratio).toFixed(0)}m`, hPt.x + 6, hPt.y + 3);
      });

      // 🔴 3D 軌跡ライン
      if (data.length > 0) {
        ctx.strokeStyle = "#ff2222"; ctx.lineWidth = 2.5; ctx.beginPath();
        data.forEach((p, idx) => {
          const pt = project3D(p.px, p.py, p.altitude, maxRange);
          if (idx === 0) ctx.moveTo(pt.x, pt.y); else ctx.lineTo(pt.x, pt.y);
        });
        ctx.stroke();

        const last = data[data.length - 1];
        const lastPt = project3D(last.px, last.py, last.altitude, maxRange);
        ctx.fillStyle = "rgba(255, 34, 34, 0.3)"; ctx.beginPath(); ctx.arc(lastPt.x, lastPt.y, 10, 0, Math.PI * 2); ctx.fill();
        ctx.fillStyle = "#ff5555"; ctx.beginPath(); ctx.arc(lastPt.x, lastPt.y, 4, 0, Math.PI * 2); ctx.fill();
      }
    };

    let isDragging = false; let prevX = 0; let prevY = 0;
    const startDrag = (x: number, y: number) => { isDragging = true; prevX = x; prevY = y; };
    const moveDrag = (x: number, y: number) => {
      if (!isDragging) return;
      const dx = x - prevX; const dy = y - prevY;
      cameraRef.current.theta -= dx * 0.007;
      cameraRef.current.phi = Math.max(0.05, Math.min(Math.PI / 2 - 0.05, cameraRef.current.phi + dy * 0.007));
      prevX = x; prevY = y; draw3DScene();
    };
    const stopDrag = () => { isDragging = false; };
    const onMouseDown = (e: MouseEvent) => startDrag(e.clientX, e.clientY);
    const onMouseMove = (e: MouseEvent) => moveDrag(e.clientX, e.clientY);
    const onTouchStart = (e: TouchEvent) => { if (e.touches.length === 1) startDrag(e.touches[0].clientX, e.touches[0].clientY); };
    const onTouchMove = (e: TouchEvent) => { if (e.touches.length === 1) moveDrag(e.touches[0].clientX, e.touches[0].clientY); };
    const onWheel = (e: WheelEvent) => { e.preventDefault(); cameraRef.current.zoomFactor = Math.max(0.3, Math.min(8.0, cameraRef.current.zoomFactor - e.deltaY * 0.0015)); draw3DScene(); };

    canvas.addEventListener("mousedown", onMouseDown); window.addEventListener("mousemove", onMouseMove); window.addEventListener("mouseup", stopDrag);
    canvas.addEventListener("touchstart", onTouchStart, { passive: true }); canvas.addEventListener("touchmove", onTouchMove, { passive: true }); canvas.addEventListener("touchend", stopDrag);
    canvas.addEventListener("wheel", onWheel, { passive: false });

    draw3DScene();

    return () => {
      canvas.removeEventListener("mousedown", onMouseDown); window.removeEventListener("mousemove", onMouseMove); window.removeEventListener("mouseup", stopDrag);
      canvas.removeEventListener("touchstart", onTouchStart); canvas.removeEventListener("touchmove", onTouchMove); canvas.removeEventListener("touchend", stopDrag);
      canvas.removeEventListener("wheel", onWheel);
    };
  }, [chartData]);

  return (
    <div className="grid grid-cols-1 lg:grid-cols-4 gap-6">
      {/* 左サイドバー */}
      <div className="lg:col-span-1 flex flex-col gap-4">
        <div className="bg-slate-900 p-5 rounded-xl border border-slate-800 shadow-lg">
          <div className="flex items-center justify-between text-slate-400 text-xs uppercase font-semibold tracking-wider mb-2">
            <span>Estimated Altitude</span>
            <Gauge size={16} className="text-emerald-400" />
          </div>
          <div className="text-4xl font-mono font-bold text-emerald-400 flex items-baseline gap-1">
            {latestSample ? latestSample.pz.toFixed(2) : "0.00"}<span className="text-xs text-slate-500 font-sans">m</span>
          </div>
        </div>

        <div className="bg-slate-900 p-5 rounded-xl border border-slate-800 shadow-lg">
          <div className="flex items-center justify-between text-slate-400 text-xs uppercase font-semibold tracking-wider mb-2">
            <span>Velocity (Z-axis)</span>
            <Activity size={16} className="text-sky-400" />
          </div>
          <div className="text-4xl font-mono font-bold text-sky-400 flex items-baseline gap-1">
            {latestSample ? latestSample.vz.toFixed(2) : "0.00"}<span className="text-xs text-slate-500 font-sans">m/s</span>
          </div>
        </div>

        <div className="bg-slate-900 p-5 rounded-xl border border-slate-800 shadow-lg">
          <div className="flex items-center justify-between text-slate-400 text-xs uppercase font-semibold tracking-wider mb-2">
            <span>Absolute Heading</span>
            <Compass size={16} className="text-amber-400" />
          </div>
          <div className="text-4xl font-mono font-bold text-amber-400 flex items-baseline gap-1">
            {latestSample ? latestSample.heading.toFixed(1) : "0.0"}<span className="text-xs text-slate-500 font-sans">°</span>
          </div>
        </div>

        <div className="bg-slate-900 p-5 rounded-xl border border-slate-800 shadow-lg flex-grow flex flex-col min-h-[220px]">
          <div className="flex items-center justify-between text-slate-400 text-xs uppercase font-semibold tracking-wider mb-3">
            <span>Onboard Cam (OV3660)</span>
            <ImageIcon size={16} className="text-slate-500" />
          </div>
          <div className="flex-grow border border-dashed border-slate-800 rounded-lg flex flex-col items-center justify-center bg-slate-950/60 p-2 overflow-hidden relative">
            {imageUrl ? (
              <Image src={imageUrl} alt="Rocket Onboard" fill className="object-cover rounded-md" unoptimized />
            ) : (
              <div className="text-center p-4">
                <p className="text-xs text-slate-500 mb-1">No Batch Image Associated</p>
                <p className="text-[10px] text-slate-700">Waiting for image injection pipeline</p>
              </div>
            )}
          </div>
        </div>
      </div>

      {/* 右メインエリア */}
      <div className="lg:col-span-3 flex flex-col gap-6">

        {/* ---- 3D空間軌跡プロッター ---- */}
        <div className="bg-slate-900 p-5 rounded-xl border border-slate-800 shadow-lg">
          <div className="flex items-center justify-between border-b border-slate-800 pb-2 mb-4">
            <h2 className="text-sm font-semibold text-slate-400 uppercase tracking-wider flex items-center gap-2">
              <RocketIcon className="w-5 h-5 animate-pulse" />
              Interactive 3D Spatial Trajectory Plotter
            </h2>
            <span className="text-[10px] text-slate-500 font-sans bg-slate-950 px-2 py-0.5 rounded border border-slate-800">
              🖱️ Drag to Rotate / Wheel to Zoom
            </span>
          </div>

          <div className="flex flex-col gap-6">
            <div className="bg-gray-950 p-1 rounded-lg border border-slate-800 shadow-inner cursor-grab active:cursor-grabbing select-none overflow-hidden mx-auto w-full flex justify-center">
              <canvas ref={canvasRef} width={700} height={520} className="bg-gray-950 block max-w-full h-auto" />
            </div>

            <div className="grid grid-cols-2 md:grid-cols-4 gap-3 w-full">
              <div className="bg-slate-950/50 p-3 rounded-lg border border-slate-800 font-mono">
                <div className="text-[10px] text-slate-500 uppercase">Downrange X (E/W)</div>
                <div className="text-lg font-bold text-slate-300 mt-1">{latestSample ? `${latestSample.px.toFixed(2)}` : "0.00"}m</div>
              </div>
              <div className="bg-slate-950/50 p-3 rounded-lg border border-slate-800 font-mono">
                <div className="text-[10px] text-slate-500 uppercase">Downrange Y (N/S)</div>
                <div className="text-lg font-bold text-slate-300 mt-1">{latestSample ? `${latestSample.py.toFixed(2)}` : "0.00"}m</div>
              </div>
              <div className="bg-slate-950/50 p-3 rounded-lg border border-slate-800 font-mono">
                <div className="text-[10px] text-slate-500 uppercase">Altitude Z (pz)</div>
                <div className="text-lg font-bold text-emerald-400 mt-1">{latestSample ? `${latestSample.pz.toFixed(2)}` : "0.00"}m</div>
              </div>
              <div className="bg-slate-950/50 p-3 rounded-lg border border-slate-800 font-mono">
                <div className="text-[10px] text-slate-500 uppercase">3D Distance from Pad</div>
                <div className="text-lg font-bold text-sky-400 mt-1">
                  {latestSample ? Math.sqrt(latestSample.px * latestSample.px + latestSample.py * latestSample.py + latestSample.pz * latestSample.pz).toFixed(2) : "0.00"}m
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* ---- グラフパネル ---- */}
        <div className="bg-slate-900 p-6 rounded-xl border border-slate-800 shadow-lg flex flex-col gap-6">
          <div className="flex justify-between items-center border-b border-slate-800 pb-2">
            <h2 className="text-sm font-semibold text-slate-400 uppercase tracking-wider">Real-time Stream Analysis ({chartData.length} pts)</h2>
            <span className="text-xs font-mono text-slate-500">Session ID: {currentBatchId}</span>
          </div>

          <div className="h-[180px] w-full">
            <span className="text-xs text-emerald-400 font-medium block mb-1">Altitude Trajectory (pz)</span>
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={chartData}>
                <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
                <XAxis dataKey="time" stroke="#475569" fontSize={10} />
                <YAxis stroke="#475569" fontSize={10} domain={["auto", "auto"]} />
                <Tooltip contentStyle={{ backgroundColor: "#0f172a", borderColor: "#1e293b", color: "#f8fafc" }} />
                <Line type="monotone" dataKey="altitude" stroke="#10b981" strokeWidth={2} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          </div>

          <div className="h-[180px] w-full">
            <span className="text-xs text-sky-400 font-medium block mb-1">Dynamics (Red: az [G] / Blue: vz [m/s])</span>
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={chartData}>
                <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
                <XAxis dataKey="time" stroke="#475569" fontSize={10} />
                <YAxis stroke="#475569" fontSize={10} domain={["auto", "auto"]} />
                <Tooltip contentStyle={{ backgroundColor: "#0f172a", borderColor: "#1e293b", color: "#f8fafc" }} />
                <Line type="monotone" dataKey="accelZ" stroke="#f87171" strokeWidth={1} dot={false} isAnimationActive={false} />
                <Line type="monotone" dataKey="velocityZ" stroke="#38bdf8" strokeWidth={2} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

      </div>
    </div>
  );
}

// 🌐 プロッターの左上で明滅させるためのインラインSVGコンポーネント (React用に属性名を完全にキャメルケースに修正)
function RocketIcon({ className }: { className?: string }) {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" className={className}>
      <g transform="translate(40, 40)">
        <line x1="0" y1="80"  x2="432" y2="296" stroke="#00ff66" strokeWidth="6" opacity="0.4" />
        <line x1="0" y1="140" x2="432" y2="356" stroke="#00ff66" strokeWidth="6" opacity="0.4" />
        <line x1="0" y1="200" x2="432" y2="416" stroke="#00ff66" strokeWidth="6" opacity="0.4" />
        <line x1="0" y1="260" x2="432" y2="476" stroke="#00ff66" strokeWidth="6" opacity="0.4" />
        <line x1="0" y1="180" x2="432" y2="-36" stroke="#00ff66" strokeWidth="6" opacity="0.4" />
        <line x1="0" y1="240" x2="432" y2="24"  stroke="#00ff66" strokeWidth="6" opacity="0.4" />
        <line x1="0" y1="300" x2="432" y2="84"  stroke="#00ff66" strokeWidth="6" opacity="0.4" />
        <line x1="0" y1="360" x2="432" y2="144" stroke="#00ff66" strokeWidth="6" opacity="0.4" />
        <line x1="0" y1="420" x2="432" y2="204" stroke="#00ff66" strokeWidth="6" opacity="0.4" />

        <line x1="160" y1="60" x2="160" y2="380" stroke="#00ff66" strokeWidth="36" strokeLinecap="round" />
        <line x1="160" y1="220" x2="340" y2="310" stroke="#00ff66" strokeWidth="36" strokeLinecap="round" />

        <path d="M 160,220 Q 230,120 360,60" fill="none" stroke="#ff2222" strokeWidth="32" strokeLinecap="round" />
        <path d="M 160,220 Q 230,120 360,60" fill="none" stroke="#ff5555" strokeWidth="12" strokeLinecap="round" />

        <g transform="translate(360, 60) rotate(65)">
          <path d="M 0,-40 L 28,28 L 0,14 L -28,28 Z" fill="#ffffff" stroke="#111111" strokeWidth="6" strokeLinejoin="round" />
        </g>
      </g>
    </svg>
  );
}