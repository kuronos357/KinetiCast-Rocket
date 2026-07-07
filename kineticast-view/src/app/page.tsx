"use client";

import dynamic from "next/dynamic";
import { Rocket } from "lucide-react"; // ✅ インポート

// クライアントサイドでのみ実行されるようにViewerを動的ロード
const TelemetryViewer = dynamic(() => import("@/components/Viewer"), {
  ssr: false,
  loading: () => (
    <div className="flex items-center justify-center min-h-[400px] text-slate-500 font-mono">
      Initializing Telemetry Pipeline Stream...
    </div>
  ),
});

export default function Home() {
  return (
    <main className="min-h-screen bg-slate-950 text-slate-50 p-6 selection:bg-emerald-500 selection:text-black">
      {/* ✅ グローバルヘッダー */}
      <header className="flex flex-col sm:flex-row justify-between items-start sm:items-center mb-8 border-b border-slate-900 pb-4 gap-4">
        <div>
          {/* ✅ Rocketアイコンをタイトル横に使用 */}
          <h1 className="text-xl font-bold flex items-center gap-2 tracking-tight">
            <Rocket className="text-emerald-400 animate-pulse" size={22} />
            KinetiCast Rocket Telemetry System
          </h1>
          <p className="text-xs text-slate-500 mt-0.5 font-mono">Project Core Directory: /kineticast-view</p>
        </div>
        <div className="flex items-center gap-2 bg-slate-900 px-3 py-1.5 rounded-full border border-slate-800 text-xs font-mono">
          <span className="w-2 height-2 rounded-full bg-emerald-500 animate-ping inline-block" style={{ width: '8px', height: '8px' }}></span>
          <span className="text-slate-300">Live Station Active</span>
        </div>
      </header>

      {/* リアルタイムビュワーモジュール */}
      <TelemetryViewer />
    </main>
  );
}