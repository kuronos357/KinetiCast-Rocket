"use client";

import { useEffect, useState, useRef } from "react";
import { query, limit, onSnapshot, collection, orderBy } from "firebase/firestore";
import { db } from "@/lib/firebase";
import { Image as ImageIcon, Video, Layers, Clock, ShieldCheck } from "lucide-react";
import Image from "next/image";

export default function Viewer() {
  const [currentSessionId, setCurrentSessionId] = useState<string>("Waiting for Session...");
  const [imageUrl, setImageUrl] = useState<string | null>(null);
  const [fps, setFps] = useState<number>(0);
  const [frameCount, setFrameCount] = useState<number>(0);
  const [history, setHistory] = useState<string[]>([]); 

  const lastFrameTimeRef = useRef<number>(Date.now());

  useEffect(() => {
    const sessionQuery = query(collection(db, "sessions"), orderBy("createdAt", "desc"), limit(1));

    const unsubscribe = onSnapshot(sessionQuery, (snapshot) => {
      if (!snapshot.empty) {
        const sessionDoc = snapshot.docs[0];
        setCurrentSessionId(sessionDoc.id);
        const newUrl = sessionDoc.data().imageUrl;
        
        if (newUrl) {
          setImageUrl(newUrl);
          const now = Date.now();
          const diff = now - lastFrameTimeRef.current;
          if (diff > 0) {
            setFps((prev) => Math.round(prev * 0.7 + (1000 / diff) * 0.3));
          }
          lastFrameTimeRef.current = now;
          setFrameCount((prev) => prev + 1);
          setHistory((prev) => prev.includes(newUrl) ? prev : [newUrl, ...prev.slice(0, 7)]);
        }
      }
    });
    return () => unsubscribe();
  }, []);

  return (
    <div className="grid grid-cols-1 xl:grid-cols-4 gap-6 w-full flex-grow">
      <div className="xl:col-span-3 flex flex-col gap-4">
        <div className="bg-slate-900 p-4 rounded-xl border border-slate-800 shadow-lg min-h-[500px] flex flex-col">
          <div className="flex justify-between border-b border-slate-800 pb-3 mb-4 text-xs font-mono">
            <span className="flex items-center gap-2 text-slate-400 font-bold tracking-wider">
              <span className="flex h-2 w-2 relative">
                <span className="animate-ping absolute h-full w-full rounded-full bg-red-400 opacity-75"></span>
                <span className="relative rounded-full h-2 w-2 bg-red-500"></span>
              </span>
              LIVE VIDEO DOWNLINK
            </span>
            <div className="flex gap-4 text-slate-500">
              <span className="flex items-center gap-1"><Video size={14} className="text-slate-400"/> FPS: <span className="text-emerald-400 font-bold">{fps > 30 ? 15 : fps}</span></span>
              <span className="flex items-center gap-1"><Layers size={14} className="text-slate-400"/> FRAMES: <span className="text-sky-400 font-bold">{frameCount}</span></span>
            </div>
          </div>

          <div className="flex-grow bg-slate-950/80 rounded-lg border border-slate-950 relative min-h-[400px] overflow-hidden">
            {imageUrl ? (
              <>
                <Image src={imageUrl} alt="Live" fill className="object-contain" unoptimized priority />
                <div className="absolute inset-0 pointer-events-none bg-[size:100%_4px] bg-gradient-to-b from-transparent via-emerald-500/[0.02] to-transparent border border-emerald-500/10" />
                <div className="absolute bottom-4 left-4 bg-slate-950/80 px-3 py-1.5 rounded text-[11px] font-mono text-slate-400 flex items-center gap-2">
                  <Clock size={12} className="text-emerald-400" /> {new Date().toLocaleTimeString()}
                </div>
              </>
            ) : (
              <div className="h-full flex flex-col items-center justify-center gap-3 text-slate-600 animate-pulse">
                <ImageIcon size={24} />
                <p className="text-sm font-medium text-slate-400">FEED OFFLINE</p>
              </div>
            )}
          </div>
        </div>
      </div>

      <div className="xl:col-span-1 flex flex-col gap-4">
        <div className="bg-slate-900 p-4 rounded-xl border border-slate-800 shadow-lg">
          <div className="text-[10px] font-bold text-slate-500 font-mono mb-2 flex justify-between">
            <span>Active Pipeline</span> <ShieldCheck size={14} className="text-emerald-500" />
          </div>
          <div className="bg-slate-950 p-2.5 rounded text-xs text-slate-300 font-mono break-all">{currentSessionId}</div>
        </div>
        
        <div className="bg-slate-900 p-4 rounded-xl border border-slate-800 flex-grow min-h-[350px]">
          <h3 className="text-xs font-bold text-slate-400 font-mono border-b border-slate-800 pb-2 mb-3">Capture Streams History</h3>
          <div className="grid grid-cols-2 gap-2 max-h-[480px] overflow-y-auto">
            {history.map((url, i) => (
              <div key={i} onClick={() => setImageUrl(url)} className="aspect-video relative rounded border border-slate-800 bg-slate-950 cursor-pointer overflow-hidden hover:border-sky-500">
                <Image src={url} alt={`Cap${i}`} fill className="object-cover" unoptimized />
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}