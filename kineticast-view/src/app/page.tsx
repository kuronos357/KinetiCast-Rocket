"use client";

import dynamic from "next/dynamic";
import { Rocket } from "lucide-react";

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
      <header className="flex flex-col sm:flex-row justify-between items-start sm:items-center mb-8 border-b border-slate-900 pb-4 gap-4">
        <div>
          <h1 className="text-xl font-bold flex items-center gap-2 tracking-tight">
            <Rocket className="text-emerald-400 animate-pulse" size={22} />
            KinetiCast Rocket Telemetry System
          </h1>
          <p className="text-xs text-slate-500 mt-0.5 font-mono">Project Core Directory: /kineticast-view</p>
        </div>

        <div className="flex flex-col gap-1 text-xs font-mono text-right">
          <a href="https://github.com/kuronos357" target="_blank" rel="noopener noreferrer" className="text-slate-400 hover:text-emerald-400 transition-colors">GitHub: github.com/kuronos357</a>
          <a href="https://x.com/Im_kairos01" target="_blank" rel="noopener noreferrer" className="text-slate-400 hover:text-emerald-400 transition-colors">X: @Im_kairos01</a>
          <a href="https://kineti-cast-rocket.vercel.app/" target="_blank" rel="noopener noreferrer" className="text-slate-500 hover:text-emerald-400 transition-colors">kineti-cast-rocket.vercel.app</a>
        </div>
      </header>

      <TelemetryViewer />
    </main>
  );
}