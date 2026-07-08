import Viewer from "@/components/Viewer";

export default function Home() {
  return (
    <main className="min-h-screen bg-slate-950 text-slate-100 p-4 md:p-8 flex flex-col gap-6">
      {/* 🌐 トップヘッダーエリア */}
      <header className="flex flex-col md:flex-row md:items-center justify-between border-b border-slate-800 pb-4 gap-4">
        <div className="flex items-center gap-3">
          {/* パーフェクトカスタムアイコン */}
          <RocketIcon className="w-8 h-8 text-emerald-400 animate-pulse" />
          <div>
            <h1 className="text-xl font-bold tracking-tight bg-gradient-to-r from-emerald-400 to-sky-400 bg-clip-text text-transparent">
              KinetiCast Rocket System
            </h1>
            <p className="text-xs text-slate-500 font-mono">
              Hybrid Solid-Liquid Telemetry Stream Engine v2.4
            </p>
          </div>
        </div>

        {/* 右側：リンクとステータスインジケーターをまとめたエリア */}
        <div className="flex flex-col items-end gap-2 self-start md:self-auto w-full md:w-auto">
          {/* 🔗 右上に配置した made by ソーシャルリンク（指定URLを反映） */}
          <div className="flex items-center gap-3 text-xs text-slate-500 font-mono mb-1 bg-slate-900/30 px-3 py-1 rounded-lg border border-slate-900/50">
            <span className="text-[10px] text-slate-600 font-sans">made by 奥平和哲</span>
            <a 
              href="https://github.com/kuronos357" 
              target="_blank" 
              rel="noopener noreferrer" 
              className="flex items-center gap-1 hover:text-slate-300 transition-colors"
            >
              <GithubIcon className="w-3.5 h-3.5 fill-current" />
              GitHub
            </a>
            <span className="text-slate-800">/</span>
            <a 
              href="https://x.com/Im_kairos01" 
              target="_blank" 
              rel="noopener noreferrer" 
              className="flex items-center gap-1 hover:text-sky-400 transition-colors"
            >
              <XIcon className="w-3 h-3 fill-current" />
              Twitter
            </a>
          </div>

          {/* ステータスインジケーター ＆ LIVE URL */}
          <div className="flex flex-col sm:flex-row sm:items-center gap-3 md:gap-4 bg-slate-900/60 px-4 py-2.5 rounded-xl border border-slate-800/80 backdrop-blur-sm w-full md:w-auto">
            <div className="flex items-center justify-between sm:justify-start gap-4 flex-grow">
              <div className="flex items-center gap-2">
                <span className="relative flex h-2 w-2">
                  <span className="animate-ping absolute inline-flex h-full w-full rounded-full bg-emerald-400 opacity-75"></span>
                  <span className="relative inline-flex rounded-full h-2 w-2 bg-emerald-500"></span>
                </span>
                <span className="text-xs font-mono text-slate-400">GND_STATION_OK</span>
              </div>
              <div className="h-4 w-px bg-slate-800 hidden sm:block"></div>
              <div className="flex items-center gap-2">
                <span className="text-xs font-mono text-slate-500">UPLINK:</span>
                <span className="text-xs font-mono text-emerald-400 font-bold">5.8GHz</span>
              </div>
            </div>

            <div className="h-px sm:h-4 w-full sm:w-px bg-slate-800/80"></div>

            {/* 🌐 追加された自身の本番サイトURLリンク */}
            <div className="flex items-center gap-1.5 text-xs font-mono text-slate-500">
              <span className="text-[10px] text-slate-600 font-sans">LIVE:</span>
              <a 
                href="https://kineti-cast-rocket.vercel.app/" 
                target="_blank" 
                rel="noopener noreferrer" 
                className="text-sky-500/90 hover:text-sky-400 transition-colors underline underline-offset-4 decoration-sky-500/30 hover:decoration-sky-400/60"
              >
                kineti-cast-rocket.vercel.app
              </a>
            </div>
          </div>
        </div>
      </header>

      {/* テレメトリコアコンポーネント */}
      <Viewer />
    </main>
  );
}

// 🌐 ページ最上部でブランドイメージとして美しく機能するインラインSVGアイコン
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

// 🐙 GitHub公式ロゴ（インラインSVG）
function GithubIcon({ className }: { className?: string }) {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" className={className}>
      <path d="M12 0c-6.626 0-12 5.373-12 12 0 5.302 3.438 9.8 8.207 11.387.599.111.793-.261.793-.577v-2.234c-3.338.726-4.033-1.416-4.033-1.416-.546-1.387-1.333-1.756-1.333-1.756-1.089-.745.083-.729.083-.729 1.205.084 1.839 1.237 1.839 1.237 1.07 1.834 2.807 1.304 3.492.997.107-.775.418-1.305.762-1.604-2.665-.305-5.467-1.334-5.467-5.931 0-1.311.469-2.381 1.236-3.221-.124-.303-.535-1.524.117-3.176 0 0 1.008-.322 3.301 1.23.957-.266 1.983-.399 3.003-.404 1.02.005 2.047.138 3.006.404 2.291-1.552 3.297-1.23 3.297-1.23.653 1.653.242 2.874.118 3.176.77.84 1.235 1.911 1.235 3.221 0 4.609-2.807 5.624-5.479 5.921.43.372.823 1.102.823 2.222v3.293c0 .319.192.694.801.576 4.765-1.589 8.199-6.086 8.199-11.386 0-6.627-5.373-12-12-12z"/>
    </svg>
  );
}

// ✖️ X (旧Twitter) 公式ロゴ（インラインSVG）
function XIcon({ className }: { className?: string }) {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" className={className}>
      <path d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-5.214-6.817L4.99 21.75H1.68l7.73-8.835L1.254 2.25H8.08l4.713 6.231zm-1.161 17.52h1.833L7.084 4.126H5.117z"/>
    </svg>
  );
}