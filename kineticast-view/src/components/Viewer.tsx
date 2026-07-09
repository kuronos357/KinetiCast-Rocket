"use client";

import { useEffect, useState, useRef } from "react";
import { query, limit, onSnapshot, collection, orderBy } from "firebase/firestore";
import { db } from "@/lib/firebase";
import { Image as ImageIcon, Video, Layers, Clock, ShieldCheck } from "lucide-react";
import Image from "next/image";

interface SessionData {
  id: string;
  imageUrl: string | null;
  updatedAt?: any;
}

export default function Viewer() {
  const [currentSessionId, setCurrentSessionId] = useState<string>("Waiting for Session...");
  const [imageUrl, setImageUrl] = useState<string | null>(null);
  const [fps, setFps] = useState<number>(0);
  const [frameCount, setFrameCount] = useState<number>(0);
  const [history, setHistory] = useState<string[]>([]); // 直近のキャプチャ履歴

  const lastFrameTimeRef = useRef<number>(Date.now());

  // ------------------------------------------
  // 📡 Firebaseリアルタイムストリーム（カメラ特化）
  // ------------------------------------------
  useEffect(() => {
    // 最新のセッションドキュメントを1件だけ監視
    const sessionQuery = query(
      collection(db, "sessions"),
      orderBy("createdAt", "desc"),
      limit(1)
    );

    const unsubscribeSession = onSnapshot(sessionQuery, (sessionSnapshot) => {
      if (!sessionSnapshot.empty) {
        const sessionDoc = sessionSnapshot.docs[0];
        const sessionId = sessionDoc.id;
        const sessionData = sessionDoc.data();
        
        setCurrentSessionId(sessionId);
        
        if (sessionData.imageUrl) {
          const newUrl = sessionData.imageUrl;
          setImageUrl(newUrl);
          
          // 📊 FPS・フレームカウンターのリアルタイム計算
          const now = Date.now();
          const diff = now - lastFrameTimeRef.current;
          if (diff > 0) {
            const calculatedFps = 1000 / diff;
            // 瞬間的な跳ね上がりを滑らかにする簡易フィルタ
            setFps((prev) => Math.round(prev * 0.7 + calculatedFps * 0.3));
          }
          lastFrameTimeRef.current = now;
          setFrameCount((prev) => prev + 1);

          // 💾 履歴スタックに追加（重複を防ぎ、最大8枚まで保持）
          setHistory((prev) => {
            if (prev.includes(newUrl)) return prev;
            return [newUrl, ...prev.slice(0, 7)];
          });
        }
      }
    });

    return () => unsubscribeSession();
  }, []);

  return (
    <div className="grid grid-cols-1 xl:grid-cols-4 gap-6 w-full flex-grow">
      
      {/* 📺 左・中央メインエリア：巨大ライブビューアー */}
      <div className="xl:col-span-3 flex flex-col gap-4">
        <div className="bg-slate-900 p-4 rounded-xl border border-slate-800 shadow-lg flex flex-col flex-grow relative overflow-hidden min-h-[500px]">
          
          {/* ヘッダーインジケーター */}
          <div className="flex items-center justify-between border-b border-slate-800 pb-3 mb-4">
            <div className="flex items-center gap-2">
              <span className="relative flex h-2 w-2">
                <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-red-400 opacity-75"></span>
                <span className="relative inline-flex rounded-full h-2 w-2 bg-red-500"></span>
              </span>
              <h2 className="text-xs font-bold uppercase tracking-wider text-slate-400 font-mono">
                LIVE VIDEO DOWNLINK
              </h2>
            </div>
            
            {/* メタデータ */}
            <div className="flex items-center gap-4 text-xs font-mono text-slate-500">
              <span className="flex items-center gap-1">
                <Video size={14} className="text-slate-400" />
                FPS: <span className="text-emerald-400 font-bold">{fps > 30 ? 10 : fps}</span>
              </span>
              <span className="flex items-center gap-1">
                <Layers size={14} className="text-slate-400" />
                FRAMES: <span className="text-sky-400 font-bold">{frameCount}</span>
              </span>
            </div>
          </div>

          {/* 🚀 巨大画像レンダリングコンポーネント */}
          <div className="flex-grow bg-slate-950/80 rounded-lg border border-slate-950 flex items-center justify-center relative min-h-[400px] overflow-hidden group shadow-inner">
            {imageUrl ? (
              <div className="relative w-full h-full min-h-[400px]">
                <Image 
                  src={imageUrl} 
                  alt="Rocket Live Onboard Feed" 
                  fill
                  className="object-contain" 
                  unoptimized 
                  priority
                />
                {/* 画面上のオーバーレイ（サイバーグリッド風のエフェクト） */}
                <div className="absolute inset-0 pointer-events-none border border-emerald-500/10 bg-gradient-to-b from-transparent via-emerald-500/[0.01] to-transparent bg-[size:100%_4px]" />
                
                {/* 左下のタイムスタンプ */}
                <div className="absolute bottom-4 left-4 bg-slate-950/80 border border-slate-800 px-3 py-1.5 rounded text-[11px] font-mono text-slate-400 backdrop-blur-sm flex items-center gap-2">
                  <Clock size={12} className="text-emerald-400" />
                  {new Date().toLocaleTimeString()}
                </div>
              </div>
            ) : (
              <div className="text-center p-8 flex flex-col items-center gap-3">
                <div className="w-12 h-12 rounded-full bg-slate-900 border border-slate-800 flex items-center justify-center text-slate-600 animate-pulse">
                  <ImageIcon size={24} />
                </div>
                <div>
                  <p className="text-sm font-medium text-slate-400">FEED OFFLINE</p>
                  <p className="text-xs text-slate-600 font-mono mt-1">Awaiting active Firebase storage upload stream...</p>
                </div>
              </div>
            )}
          </div>
        </div>
      </div>

      {/* 📊 右サイドバー：セッション情報 ＆ 撮影キャプチャ履歴 */}
      <div className="xl:col-span-1 flex flex-col gap-4">
        
        {/* セッションステータス */}
        <div className="bg-slate-900 p-4 rounded-xl border border-slate-800 shadow-lg">
          <div className="text-[10px] uppercase font-bold tracking-wider text-slate-500 font-mono mb-2 flex items-center justify-between">
            <span>Active Pipeline</span>
            <ShieldCheck size={14} className="text-emerald-500" />
          </div>
          <div className="bg-slate-950 p-2.5 rounded border border-slate-800/60 font-mono text-xs text-slate-300 break-all select-all">
            {currentSessionId}
          </div>
        </div>

        {/* 📸 過去の撮影フレーム（履歴タイル） */}
        <div className="bg-slate-900 p-4 rounded-xl border border-slate-800 shadow-lg flex-grow flex flex-col min-h-[350px]">
          <h3 className="text-xs font-bold uppercase tracking-wider text-slate-400 font-mono border-b border-slate-800 pb-2 mb-3">
            Capture Streams History
          </h3>
          
          <div className="grid grid-cols-2 gap-2 overflow-y-auto max-h-[480px] pr-1 flex-grow scrollbar-thin scrollbar-thumb-slate-800">
            {history.length > 0 ? (
              history.map((url, index) => (
                <div 
                  key={index} 
                  onClick={() => setImageUrl(url)} // クリックでメインビューにピン留め可能
                  className={`aspect-video relative rounded border cursor-pointer overflow-hidden transition-all duration-200 bg-slate-950 hover:border-sky-500 group ${
                    imageUrl === url ? 'border-emerald-500 ring-2 ring-emerald-500/20' : 'border-slate-800'
                  }`}
                >
                  <Image 
                    src={url} 
                    alt={`Capture ${index}`} 
                    fill 
                    className="object-cover group-hover:scale-105 transition-transform"
                    unoptimized 
                  />
                  <div className="absolute top-1 left-1 bg-slate-950/80 px-1 rounded text-[9px] font-mono text-slate-400">
                    #{history.length - index}
                  </div>
                </div>
              ))
            ) : (
              <div className="col-span-2 text-center py-12 text-xs text-slate-600 font-mono">
                No frames cached yet
              </div>
            )}
          </div>
        </div>
      </div>

    </div>
  );
}