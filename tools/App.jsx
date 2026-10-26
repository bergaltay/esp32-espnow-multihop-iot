/* global React, ReactDOM, Recharts, framerMotion, TweaksPanel, useTweaks, TweakSection, TweakToggle, TweakRadio, TweakButton, TweakSlider */
const { useState, useEffect, useRef, useMemo, useCallback } = React;
const FM = window.framerMotion || window.Motion || window.FramerMotion || {};
const { motion, AnimatePresence } = FM;
const { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Legend } = Recharts;

/* ================== i18n ================== */
const I18N = {
  tr: {
    title: "Smart Agriculture IoT",
    live: "Canlı",
    offline: "Bağlantı Yok",
    avgTemp: "Ortalama Sıcaklık",
    avgHumidity: "Ortalama Nem",
    minBattery: "En Düşük Batarya",
    activeNodes: "Aktif Node",
    topology: "Ağ Topolojisi",
    nodeA: "Node A",
    nodeB: "Node B",
    gateway: "Gateway",
    temp: "Sıcaklık",
    humidity: "Nem",
    soilMoisture: "Toprak Nemi",
    battery: "Batarya",
    sparkTitle: "Son 10 Ölçüm",
    secondsAgo: "saniye önce",
    justNow: "az önce",
    flagFrost: "DON",
    flagBrownout: "DÜŞÜK GÜÇ",
    flagSkip: "ATLAMA",
    eventLog: "Olay Günlüğü",
    pktReceived: "Paket alındı",
    flagRaised: "Bayrak yükseldi",
    flagCleared: "Bayrak temizlendi",
    min: "MIN",
    max: "MAX",
    agg: "Agg",
    hop: "Hop",
    rssi: "RSSI",
    waitingData: "Veri bekleniyor…",
    relay: "Röle",
    edge: "Uç",
    hub: "Merkez"
  },
  en: {
    title: "Smart Agriculture IoT",
    live: "Live",
    offline: "Offline",
    avgTemp: "Average Temperature",
    avgHumidity: "Average Humidity",
    minBattery: "Lowest Battery",
    activeNodes: "Active Nodes",
    topology: "Network Topology",
    nodeA: "Node A",
    nodeB: "Node B",
    gateway: "Gateway",
    temp: "Temperature",
    humidity: "Humidity",
    soilMoisture: "Soil Moisture",
    battery: "Battery",
    sparkTitle: "Last 10 Readings",
    secondsAgo: "s ago",
    justNow: "just now",
    flagFrost: "FROST",
    flagBrownout: "BROWNOUT",
    flagSkip: "NODE SKIP",
    eventLog: "Event Log",
    pktReceived: "Packet received",
    flagRaised: "Flag raised",
    flagCleared: "Flag cleared",
    min: "MIN",
    max: "MAX",
    agg: "Agg",
    hop: "Hop",
    rssi: "RSSI",
    waitingData: "Waiting for data…",
    relay: "Relay",
    edge: "Edge",
    hub: "Hub"
  }
};

/* ================== Lucide Icon (React wrapper around UMD) ================== */
function Icon({ name, size = 20, color = "currentColor", strokeWidth = 2, style }) {
  const node = window.lucide && window.lucide.icons && window.lucide.icons[name];
  if (!node) {
    return <span style={{ display: "inline-block", width: size, height: size, ...style }} />;
  }
  const renderNode = (n, key) => {
    if (typeof n === "string") return n;
    const [tag, attrs, children] = n;
    return React.createElement(
      tag, { ...attrs, key },
      Array.isArray(children) ? children.map((c, i) => renderNode(c, i)) : null
    );
  };
  return (
    <svg
      xmlns="http://www.w3.org/2000/svg"
      width={size} height={size} viewBox="0 0 24 24"
      fill="none" stroke={color} strokeWidth={strokeWidth}
      strokeLinecap="round" strokeLinejoin="round"
      style={style}>
      
      {Array.isArray(node[2]) ? node[2].map((c, i) => renderNode(c, i)) : null}
    </svg>);

}

/* ================== Mock Data ================== */
function makeInitial(nodeId) {
  const base = {
    1: { soil: 52.3, t: 22.4, h: 67, bp: 78, r: -42, hop: 1 },
    2: { soil: 44.9, t: 21.4, h: 71, bp: 54, r: -46, hop: 2 }
  }[nodeId];
  return {
    node_id: nodeId,
    timestamp_ms: Date.now(),
    soil_moisture: base.soil,
    air_temp_c: base.t,
    air_humidity: base.h,
    battery_mv: 3000 + base.bp * 12,
    battery_pct: base.bp,
    rssi_dbm: base.r,
    rx_rssi_dbm: base.r - 8,
    hop_count: base.hop,
    agg_count: 1,
    flags: { frost: false, brownout: false, node_skip: false },
    receivedAt: Date.now()
  };
}
function dither(v, range, min, max) {
  return Math.max(min, Math.min(max, v + (Math.random() - 0.5) * range));
}
function nextPacket(prev, nodeId, forcedFlags) {
  const t = dither(prev.air_temp_c, 0.6, -5, 35);
  const h = dither(prev.air_humidity, 2, 20, 100);
  const sm = dither(prev.soil_moisture, 1.5, 5, 95);
  const bp = Math.max(8, prev.battery_pct - (Math.random() < 0.04 ? 1 : 0));
  const bm = 3000 + bp * 12;
  const r = dither(prev.rssi_dbm, 4, -90, -30);
  const flags = forcedFlags ? { ...forcedFlags } : {
    frost: t < 2,
    brownout: bm < 3300,
    node_skip: Math.random() < 0.025
  };
  return {
    node_id: nodeId,
    timestamp_ms: Date.now(),
    soil_moisture: +sm.toFixed(2),
    air_temp_c: +t.toFixed(1),
    air_humidity: Math.round(h),
    battery_mv: Math.round(bm),
    battery_pct: bp,
    rssi_dbm: Math.round(r),
    rx_rssi_dbm: Math.round(r) - 8,
    hop_count: prev.hop_count,
    agg_count: 1,
    flags,
    receivedAt: Date.now()
  };
}

/* ================== Live "X seconds ago" ================== */
function useTick(ms = 1000) {
  const [, setN] = useState(0);
  useEffect(() => {
    const id = setInterval(() => setN((n) => n + 1), ms);
    return () => clearInterval(id);
  }, [ms]);
}
function timeAgo(receivedAt, t) {
  if (!receivedAt) return "—";
  const sec = Math.max(0, Math.floor((Date.now() - receivedAt) / 1000));
  if (sec < 1) return t.justNow;
  return `${sec} ${t.secondsAgo}`;
}

/* ================== Header ================== */
function Header({ t, connected, lang, setLang }) {
  return (
    <header style={{
      width: "100%",
      background: "var(--bg-card)",
      borderBottom: "1px solid var(--border)",
      padding: "18px 32px",
      display: "flex",
      alignItems: "center",
      justifyContent: "space-between",
      position: "sticky",
      top: 0,
      zIndex: 10
    }}>
      <div style={{ display: "flex", alignItems: "center", gap: 14 }}>
        <div style={{
          width: 38, height: 38, borderRadius: 11,
          background: "var(--accent-green)",
          display: "grid", placeItems: "center",
          boxShadow: "0 4px 12px rgba(92,138,60,0.25)"
        }}>
          <Icon name="Leaf" size={20} color="white" strokeWidth={2.2} />
        </div>
        <div>
          <div style={{ fontSize: 22, fontWeight: 700, letterSpacing: "-0.01em", lineHeight: 1.1 }}>
            {t.title}
          </div>
          <div style={{ fontSize: 12, color: "var(--text-secondary)", marginTop: 2 }}>
            Mesh Network · 2 Nodes · 1 Gateway
          </div>
        </div>
      </div>
      <div style={{ display: "flex", alignItems: "center", gap: 16 }}>
        <div style={{
          display: "flex",
          background: "var(--bg-main)",
          border: "1px solid var(--border)",
          borderRadius: 999,
          padding: 3,
          fontSize: 12,
          fontWeight: 600
        }}>
          {["tr", "en"].map((L) =>
          <button key={L}
          onClick={() => setLang(L)}
          style={{
            border: "none",
            background: lang === L ? "var(--bg-card)" : "transparent",
            color: lang === L ? "var(--text-primary)" : "var(--text-secondary)",
            padding: "6px 12px",
            borderRadius: 999,
            cursor: "pointer",
            boxShadow: lang === L ? "var(--shadow-card)" : "none",
            textTransform: "uppercase",
            letterSpacing: "0.04em"
          }}>{L}</button>
          )}
        </div>
        <div style={{
          display: "flex", alignItems: "center", gap: 10,
          background: connected ? "var(--accent-green-soft)" : "#EFE9DC",
          padding: "8px 14px",
          borderRadius: 999
        }}>
          <span className={"live-dot" + (connected ? "" : " off")} />
          <span style={{
            fontSize: 13, fontWeight: 600,
            color: connected ? "var(--accent-green)" : "var(--text-secondary)"
          }}>
            {connected ? t.live : t.offline}
          </span>
        </div>
      </div>
    </header>);

}

/* ================== Summary Cards ================== */
function SummaryCard({ iconName, iconBg, iconColor, label, value, suffix, sub }) {
  return (
    <div style={{
      flex: 1,
      background: "var(--bg-card)",
      border: "1px solid var(--border)",
      borderRadius: 20,
      padding: 24,
      boxShadow: "var(--shadow-card)",
      display: "flex",
      flexDirection: "column",
      gap: 18,
      minHeight: 156
    }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start" }}>
        <div style={{
          width: 44, height: 44, borderRadius: 12,
          background: iconBg,
          display: "grid", placeItems: "center"
        }}>
          <Icon name={iconName} size={22} color={iconColor} strokeWidth={2.2} />
        </div>
        {sub &&
        <div style={{ fontSize: 11, color: "var(--text-secondary)", fontWeight: 500, textAlign: "right" }}>
            {sub}
          </div>
        }
      </div>
      <div>
        <div style={{ fontSize: 44, fontWeight: 700, lineHeight: 1, letterSpacing: "-0.02em" }}>
          {value}
          {suffix && <span style={{ fontSize: 22, fontWeight: 600, color: "var(--text-secondary)", marginLeft: 4 }}>{suffix}</span>}
        </div>
        <div className="uppercase-label" style={{ marginTop: 10 }}>{label}</div>
      </div>
    </div>);

}

function SummaryRow({ t, nodeA, nodeB }) {
  const temps = [nodeA?.air_temp_c, nodeB?.air_temp_c].filter((v) => v != null);
  const hums = [nodeA?.air_humidity, nodeB?.air_humidity].filter((v) => v != null);
  const bats = [nodeA?.battery_pct, nodeB?.battery_pct].filter((v) => v != null);
  const avgT = temps.length ? (temps.reduce((a, b) => a + b, 0) / temps.length).toFixed(1) : "--";
  const avgH = hums.length ? Math.round(hums.reduce((a, b) => a + b, 0) / hums.length) : "--";
  const minB = bats.length ? Math.min(...bats) : "--";
  const active = (nodeA ? 1 : 0) + (nodeB ? 1 : 0);

  return (
    <div style={{ display: "flex", gap: 16 }}>
      <SummaryCard iconName="Thermometer" iconBg="var(--accent-green-soft)" iconColor="var(--accent-green)"
      value={avgT} suffix="°C" label={t.avgTemp} sub="A · B" />
      <SummaryCard iconName="Droplets" iconBg="#E3EDF5" iconColor="#3B7BB0"
      value={avgH} suffix="%" label={t.avgHumidity} sub="A · B" />
      <SummaryCard iconName="BatteryLow" iconBg="var(--accent-brown-soft)" iconColor="var(--accent-brown)"
      value={minB} suffix="%" label={t.minBattery} sub={`min(A, B)`} />
      <SummaryCard iconName="Radio" iconBg="var(--accent-green-soft)" iconColor="var(--accent-green)"
      value={`${active} / 2`} label={t.activeNodes} sub="online" />
    </div>);

}

/* ================== Topology ================== */
function Topology({ t, lastEvent }) {
  // SVG coordinates: 1376 wide canvas, 180 tall padded
  const W = 1376,H = 180;
  const yMid = H / 2;
  // 3 nodes: A (left), B (middle), Gateway (right)
  const positions = [
  { id: "A", cx: W * 0.2, cy: yMid, label: t.nodeA, sub: t.edge, icon: "Sprout" },
  { id: "B", cx: W * 0.5, cy: yMid, label: t.nodeB, sub: t.relay, icon: "Sprout" },
  { id: "GW", cx: W * 0.8, cy: yMid, label: t.gateway, sub: t.hub, icon: "RadioTower" }];


  // path generators
  const arc = (x1, y1, x2, y2) => {
    const mx = (x1 + x2) / 2;
    const my = y1 - 28; // arc up
    return `M ${x1} ${y1} Q ${mx} ${my} ${x2} ${y2}`;
  };
  // connection segments end short of node circle (radius 36 + 6)
  const offset = 42;
  const seg1 = { x1: positions[0].cx + offset, y1: yMid, x2: positions[1].cx - offset, y2: yMid };
  const seg2 = { x1: positions[1].cx + offset, y1: yMid, x2: positions[2].cx - offset, y2: yMid };
  const path1 = arc(seg1.x1, seg1.y1, seg1.x2, seg1.y2);
  const path2 = arc(seg2.x1, seg2.y1, seg2.x2, seg2.y2);

  return (
    <div style={{
      background: "var(--bg-card)",
      border: "1px solid var(--border)",
      borderRadius: 20,
      padding: "20px 32px 28px",
      boxShadow: "var(--shadow-card)"
    }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 4 }}>
        <div className="card-title">{t.topology}</div>
        <div style={{ fontSize: 12, color: "var(--text-secondary)" }}>
          A → B → Gateway
        </div>
      </div>
      <div style={{ position: "relative", width: "100%", height: H }}>
        <svg width="100%" height={H} viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="xMidYMid meet"
        style={{ position: "absolute", inset: 0 }}>
          {/* dashed connection paths */}
          <path d={path1} fill="none" stroke="var(--border)" strokeWidth="2" strokeDasharray="6 4" />
          <path d={path2} fill="none" stroke="var(--border)" strokeWidth="2" strokeDasharray="6 4" />

          {/* flowing light */}
          <AnimatePresence>
            {lastEvent && lastEvent.segment === "AB" &&
            <motion.circle
              key={`flow-AB-${lastEvent.id}`}
              r="7"
              fill="var(--accent-green)"
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              style={{ filter: "drop-shadow(0 0 8px rgba(92,138,60,0.7))" }}>
              
                <animateMotion dur="0.8s" repeatCount="1" fill="remove" path={path1} />
              </motion.circle>
            }
            {lastEvent && (lastEvent.segment === "BG" || lastEvent.segment === "AB") &&
            <motion.circle
              key={`flow-BG-${lastEvent.id}-${lastEvent.segment}`}
              r="7"
              fill="var(--accent-green)"
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              style={{ filter: "drop-shadow(0 0 8px rgba(92,138,60,0.7))" }}>
              
                <animateMotion
                dur="0.8s"
                repeatCount="1"
                fill="remove"
                begin={lastEvent.segment === "AB" ? "0.7s" : "0s"}
                path={path2} />
              
              </motion.circle>
            }
          </AnimatePresence>
        </svg>

        {/* node circles via absolute positioned divs (for nicer text rendering) */}
        {positions.map((p, i) => {
          const isGateway = p.id === "GW";
          return (
            <div key={p.id} style={{
              position: "absolute",
              left: `${p.cx / W * 100}%`,
              top: yMid,
              transform: "translate(-50%, -50%)",
              display: "flex",
              flexDirection: "column",
              alignItems: "center",
              gap: 8,
              pointerEvents: "none"
            }}>
              <motion.div
                animate={
                lastEvent && (
                lastEvent.toNode === p.id ||
                lastEvent.segment === "AB" && p.id === "B" ||
                lastEvent.segment === "BG" && p.id === "GW") ?
                { scale: [1, 1.08, 1] } : { scale: 1 }
                }
                transition={{ duration: 0.5 }}
                style={{
                  width: 72, height: 72, borderRadius: "50%",
                  background: isGateway ? "var(--accent-brown)" : "var(--accent-green)",
                  display: "grid", placeItems: "center",
                  color: "white",
                  boxShadow: isGateway ?
                  "0 6px 20px rgba(139,111,71,0.35), inset 0 -3px 0 rgba(0,0,0,0.08)" :
                  "0 6px 20px rgba(92,138,60,0.35), inset 0 -3px 0 rgba(0,0,0,0.08)"
                }}>
                
                <Icon name={p.icon} size={30} color="white" strokeWidth={2} />
              </motion.div>
              <div style={{ textAlign: "center", marginTop: 4, whiteSpace: "nowrap" }}>
                <div style={{ fontSize: 13, fontWeight: 600, color: "var(--text-primary)", whiteSpace: "nowrap" }}>
                  {p.label}
                </div>
                <div style={{ fontSize: 11, color: "var(--text-secondary)", textTransform: "uppercase", letterSpacing: "0.05em", marginTop: 2 }}>
                  {p.sub}
                </div>
              </div>
            </div>);

        })}
      </div>
    </div>);

}

/* ================== Soil Moisture Ring ================== */
function SoilRing({ value, min, max, t }) {
  const pct = Math.max(0, Math.min(100, value || 0));
  const R = 32,C = 2 * Math.PI * R;
  const dash = pct / 100 * C;
  return (
    <div style={{ display: "flex", flexDirection: "column", alignItems: "center", gap: 6 }}>
      <div style={{ position: "relative", width: 80, height: 80 }}>
        <svg width="80" height="80" viewBox="0 0 80 80">
          <circle cx="40" cy="40" r={R} fill="none" stroke="var(--border)" strokeWidth="7" />
          <circle
            cx="40" cy="40" r={R} fill="none"
            stroke="var(--accent-green)" strokeWidth="7"
            strokeLinecap="round"
            strokeDasharray={`${dash} ${C}`}
            transform="rotate(-90 40 40)"
            style={{ transition: "stroke-dasharray 0.6s ease" }} />
          
        </svg>
        <div style={{
          position: "absolute", inset: 0,
          display: "grid", placeItems: "center",
          fontSize: 16, fontWeight: 700,
          color: "var(--text-primary)", fontFamily: "Inter"
        }}>
          {Math.round(pct)}%
        </div>
      </div>
      <div style={{ fontSize: 11, color: "var(--text-secondary)", textTransform: "uppercase", letterSpacing: "0.05em" }}>
        {t.soilMoisture}
      </div>
      {min != null && max != null &&
      <div style={{ display: "flex", gap: 12, fontSize: 10, color: "var(--text-secondary)", fontVariantNumeric: "tabular-nums" }}>
          <span>{t.min} {Math.round(min)}%</span>
          <span style={{ color: "var(--border)" }}>·</span>
          <span>{t.max} {Math.round(max)}%</span>
        </div>
      }
    </div>);

}

/* ================== Battery Glyph ================== */
function BatteryGlyph({ pct, alert }) {
  const fillW = Math.max(2, pct / 100 * 32);
  const color = alert ? "var(--alert-accent)" : pct < 25 ? "var(--alert-accent)" : "var(--accent-green)";
  return (
    <svg width="48" height="22" viewBox="0 0 48 22" fill="none">
      <rect x="1" y="2" width="40" height="18" rx="4" stroke="var(--text-secondary)" strokeWidth="1.5" fill="var(--bg-card)" />
      <rect x="42" y="7" width="4" height="8" rx="1.5" fill="var(--text-secondary)" />
      <rect x="4" y="5" width={fillW} height="12" rx="2" fill={color}
      style={{ transition: "width 0.5s ease, fill 0.3s ease" }} />
    </svg>);

}

/* ================== Flag Pills ================== */
function FlagPills({ flags, t }) {
  const items = [];
  if (flags.frost) items.push({ k: "frost", icon: "Snowflake", label: t.flagFrost });
  if (flags.brownout) items.push({ k: "brownout", icon: "Zap", label: t.flagBrownout });
  if (flags.node_skip) items.push({ k: "skip", icon: "CornerUpRight", label: t.flagSkip });
  if (!items.length) return null;
  return (
    <div style={{ display: "flex", flexWrap: "wrap", gap: 6, marginTop: 8 }}>
      {items.map((it) =>
      <span key={it.k} style={{
        display: "inline-flex", alignItems: "center", gap: 5,
        background: "var(--alert-accent)",
        color: "white",
        borderRadius: 999,
        padding: "4px 10px",
        fontSize: 11,
        fontWeight: 600,
        letterSpacing: "0.03em"
      }}>
          <Icon name={it.icon} size={11} color="white" strokeWidth={2.4} />
          {it.label}
        </span>
      )}
    </div>);

}

/* ================== Node Card ================== */
function NodeCard({ data, name, t, kind /* 'edge' | 'gateway' */, pulseKey }) {
  useTick(1000); // keep "x sec ago" fresh
  const alertActive = data?.flags && (data.flags.frost || data.flags.brownout || data.flags.node_skip);
  const isGateway = kind === "gateway";

  const minSoil = data?.minSoil,maxSoil = data?.maxSoil;
  const ago = data ? timeAgo(data.receivedAt, t) : "—";

  return (
    <motion.div
      key={pulseKey ?? "card"}
      animate={data ? { scale: [1, 1.02, 1] } : { scale: 1 }}
      transition={{ duration: 0.3, ease: "easeOut" }}
      style={{
        flex: 1,
        background: alertActive ? "var(--alert-tint)" : "var(--bg-card)",
        border: `1px solid ${alertActive ? "var(--alert-accent)" : "var(--border)"}`,
        borderRadius: 20,
        padding: 24,
        boxShadow: "var(--shadow-card)",
        position: "relative",
        transition: "background 0.4s ease, border-color 0.4s ease",
        minHeight: 380,
        display: "flex",
        flexDirection: "column"
      }}>
      
      {/* Top accent stripe for gateway */}
      {isGateway && !alertActive &&
      <div style={{
        position: "absolute", top: 0, left: 24, right: 24, height: 3,
        background: "var(--accent-brown)",
        borderRadius: 999
      }} />
      }

      {/* Header row */}
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "flex-start", marginBottom: 20 }}>
        <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
          <div style={{
            width: 32, height: 32, borderRadius: 10,
            background: isGateway ? "var(--accent-brown-soft)" : "var(--accent-green-soft)",
            display: "grid", placeItems: "center"
          }}>
            <Icon
              name={isGateway ? "RadioTower" : "Sprout"}
              size={16}
              color={isGateway ? "var(--accent-brown)" : "var(--accent-green)"}
              strokeWidth={2.2} />
            
          </div>
          <div>
            <div style={{ fontSize: 16, fontWeight: 600, color: "var(--text-primary)" }}>{name}</div>
            <div style={{ fontSize: 11, color: "var(--text-secondary)", textTransform: "uppercase", letterSpacing: "0.05em" }}>
              {isGateway ? t.hub : data?.hop_count === 1 ? t.edge : t.relay}
            </div>
          </div>
        </div>
        <div style={{ fontSize: 12, color: "var(--text-secondary)", fontVariantNumeric: "tabular-nums" }}>
          {ago}
        </div>
      </div>

      {isGateway ?
      <GatewayBody data={data} t={t} /> :

      <NodeBody data={data} t={t} minSoil={minSoil} maxSoil={maxSoil} />
      }

      {!isGateway && data && <FlagPills flags={data.flags} t={t} />}
    </motion.div>);

}

function NodeBody({ data, t, minSoil, maxSoil }) {
  if (!data) return <div style={{ color: "var(--text-secondary)", fontSize: 13 }}>{t.waitingData}</div>;
  return (
    <>
      {/* Two main metrics */}
      <div style={{ display: "flex", gap: 16, marginBottom: 24 }}>
        <Metric value={`${data.air_temp_c.toFixed(1)}°`} suffix="C" label={t.temp} icon="Thermometer" />
        <Metric value={`${data.air_humidity}`} suffix="%" label={t.humidity} icon="Droplets" iconColor="#3B7BB0" iconBg="#E3EDF5" />
      </div>
      {/* Soil ring centered */}
      <div style={{ display: "flex", justifyContent: "center", marginBottom: 18 }}>
        <SoilRing value={data.soil_moisture} min={minSoil} max={maxSoil} t={t} />
      </div>
      {/* Battery + footer */}
      <div style={{
        display: "flex", justifyContent: "space-between", alignItems: "center",
        paddingTop: 16, borderTop: "1px solid var(--border)",
        marginTop: "auto"
      }}>
        <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
          <BatteryGlyph pct={data.battery_pct} alert={data.flags?.brownout} />
          <span style={{ fontSize: 14, fontWeight: 600, color: "var(--text-primary)", fontVariantNumeric: "tabular-nums" }}>
            {data.battery_pct}%
          </span>
          <span style={{ fontSize: 11, color: "var(--text-secondary)", fontVariantNumeric: "tabular-nums" }}>
            {(data.battery_mv / 1000).toFixed(2)}V
          </span>
        </div>
        <div style={{ display: "flex", flexDirection: "column", alignItems: "flex-end", fontSize: 11, color: "var(--text-secondary)", gap: 2, fontVariantNumeric: "tabular-nums" }}>
          <span>{t.rssi}: {data.rssi_dbm} dBm</span>
          <span>{t.hop}: {data.hop_count} · {t.agg}: {data.agg_count}</span>
        </div>
      </div>
    </>);

}

function GatewayBody({ data, t }) {
  if (!data) return <div style={{ color: "var(--text-secondary)", fontSize: 13 }}>{t.waitingData}</div>;
  return (
    <div style={{ display: "flex", flexDirection: "column", flex: 1 }}>
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 14, marginBottom: 16 }}>
        <KV label="Uplink" value="WiFi" mono />
        <KV label="MQTT" value="connected" valueColor="var(--accent-green)" />
        <KV label="Pkt/min" value={data.pktPerMin ?? 28} mono />
        <KV label="Loss" value="0.3%" mono />
      </div>
      <div style={{
        background: "var(--accent-brown-soft)",
        borderRadius: 14,
        padding: 16,
        marginTop: "auto"
      }}>
        <div className="uppercase-label" style={{ fontSize: 11, marginBottom: 8 }}>Mesh Health</div>
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "baseline" }}>
          <div style={{ fontSize: 28, fontWeight: 700, letterSpacing: "-0.02em" }}>
            98.7<span style={{ fontSize: 16, color: "var(--text-secondary)" }}>%</span>
          </div>
          <div style={{ fontSize: 11, color: "var(--text-secondary)" }}>
            Uptime · 14h 32m
          </div>
        </div>
        {/* mini bar */}
        <div style={{ height: 6, background: "var(--bg-card)", borderRadius: 999, marginTop: 10, overflow: "hidden" }}>
          <div style={{
            width: "98.7%", height: "100%",
            background: "var(--accent-brown)",
            borderRadius: 999
          }} />
        </div>
      </div>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center",
        marginTop: 16, paddingTop: 16, borderTop: "1px solid var(--border)",
        fontSize: 11, color: "var(--text-secondary)", fontVariantNumeric: "tabular-nums" }}>
        <span>RX from B: {data.rx_rssi_dbm} dBm</span>
        <span>2 nodes · 1 hop avg</span>
      </div>
    </div>);

}

function KV({ label, value, mono, valueColor }) {
  return (
    <div>
      <div className="uppercase-label" style={{ fontSize: 10 }}>{label}</div>
      <div style={{
        marginTop: 4,
        fontSize: 16, fontWeight: 600,
        color: valueColor || "var(--text-primary)",
        fontFamily: mono ? "ui-monospace, SFMono-Regular, Menlo, monospace" : undefined,
        fontVariantNumeric: "tabular-nums"
      }}>{value}</div>
    </div>);

}

function Metric({ value, suffix, label, icon, iconColor = "var(--accent-green)", iconBg = "var(--accent-green-soft)" }) {
  return (
    <div style={{ flex: 1 }}>
      <div style={{ display: "flex", alignItems: "center", gap: 8, marginBottom: 8 }}>
        <div style={{
          width: 24, height: 24, borderRadius: 7,
          background: iconBg,
          display: "grid", placeItems: "center"
        }}>
          <Icon name={icon} size={13} color={iconColor} strokeWidth={2.4} />
        </div>
        <div className="uppercase-label" style={{ fontSize: 11 }}>{label}</div>
      </div>
      <div style={{ fontSize: 44, fontWeight: 700, lineHeight: 1, letterSpacing: "-0.02em", fontVariantNumeric: "tabular-nums" }}>
        {value}
        {suffix && <span style={{ fontSize: 20, fontWeight: 600, color: "var(--text-secondary)", marginLeft: 2 }}>{suffix}</span>}
      </div>
    </div>);

}

/* ================== Sparklines ================== */
function SparklineBlock({ name, history, t }) {
  const data = history.map((p, i) => ({
    idx: i + 1,
    [t.temp]: p.air_temp_c,
    [t.humidity]: p.air_humidity
  }));

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
        <div style={{ fontSize: 14, fontWeight: 600, color: "var(--text-secondary)" }}>{name}</div>
        <div style={{ display: "flex", gap: 14, fontSize: 11, color: "var(--text-secondary)", fontWeight: 500 }}>
          <span style={{ display: "inline-flex", alignItems: "center", gap: 6 }}>
            <span style={{ width: 8, height: 8, borderRadius: "50%", background: "var(--accent-green)" }} />
            {t.temp}
          </span>
          <span style={{ display: "inline-flex", alignItems: "center", gap: 6 }}>
            <span style={{ width: 8, height: 8, borderRadius: "50%", background: "var(--accent-brown)" }} />
            {t.humidity}
          </span>
        </div>
      </div>
      <div style={{ height: 120, width: "100%" }}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart data={data} margin={{ top: 6, right: 8, left: 0, bottom: 0 }}>
            <CartesianGrid stroke="var(--border)" strokeDasharray="3 3" vertical={false} />
            <XAxis
              dataKey="idx"
              tick={{ fontSize: 11, fill: "var(--text-secondary)" }}
              axisLine={false}
              tickLine={false}
              padding={{ left: 6, right: 6 }} />
            
            <YAxis hide domain={['auto', 'auto']} />
            <Tooltip
              contentStyle={{
                background: "var(--bg-card)",
                border: "1px solid var(--border)",
                borderRadius: 12,
                fontSize: 12,
                boxShadow: "var(--shadow-card-lg)"
              }}
              labelStyle={{ color: "var(--text-secondary)", fontWeight: 600 }} />
            
            <Line type="monotone" dataKey={t.temp} stroke="var(--accent-green)" strokeWidth={2} dot={false} isAnimationActive={false} />
            <Line type="monotone" dataKey={t.humidity} stroke="var(--accent-brown)" strokeWidth={2} dot={false} isAnimationActive={false} />
          </LineChart>
        </ResponsiveContainer>
      </div>
    </div>);

}

function SparklineSection({ t, hist1, hist2 }) {
  return (
    <div style={{
      background: "var(--bg-card)",
      border: "1px solid var(--border)",
      borderRadius: 20,
      padding: 24,
      boxShadow: "var(--shadow-card)",
      display: "flex",
      flexDirection: "column",
      gap: 24
    }}>
      <div className="card-title">{t.sparkTitle}</div>
      <SparklineBlock name={t.nodeA} history={hist1} t={t} />
      <SparklineBlock name={t.nodeB} history={hist2} t={t} />
    </div>);

}

/* ================== Event Log ================== */
function EventLog({ events, t }) {
  return (
    <div style={{
      background: "var(--bg-card)",
      border: "1px solid var(--border)",
      borderRadius: 20,
      padding: 24,
      boxShadow: "var(--shadow-card)",
      flex: "0 0 360px",
      display: "flex",
      flexDirection: "column",
      gap: 14
    }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
        <div className="card-title">{t.eventLog}</div>
        <span style={{ fontSize: 11, color: "var(--text-secondary)", fontWeight: 500 }}>{events.length}</span>
      </div>
      <div className="event-log" style={{ display: "flex", flexDirection: "column", gap: 2 }}>
        <AnimatePresence initial={false}>
          {events.slice(0, 40).map((ev) =>
          <motion.div
            key={ev.id}
            initial={{ opacity: 0, height: 0 }}
            animate={{ opacity: 1, height: "auto" }}
            exit={{ opacity: 0 }}
            transition={{ duration: 0.25 }}
            style={{
              display: "flex",
              alignItems: "center",
              gap: 10,
              padding: "8px 0",
              borderBottom: "1px solid var(--border)"
            }}>
            
              <div style={{
              width: 6, height: 6, borderRadius: "50%",
              background: ev.kind === "alert" ? "var(--alert-accent)" : ev.kind === "clear" ? "var(--accent-brown)" : "var(--accent-green)",
              flexShrink: 0
            }} />
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ fontSize: 12, color: "var(--text-primary)", fontWeight: 500 }}>{ev.title}</div>
                <div style={{ fontSize: 11, color: "var(--text-secondary)", fontVariantNumeric: "tabular-nums" }}>
                  {ev.detail}
                </div>
              </div>
              <div style={{ fontSize: 10, color: "var(--text-secondary)", fontVariantNumeric: "tabular-nums", flexShrink: 0 }}>
                {new Date(ev.at).toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit", second: "2-digit" })}
              </div>
            </motion.div>
          )}
        </AnimatePresence>
      </div>
    </div>);

}

/* ================== Tweaks ================== */
const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "rateMs": 2800,
  "forceFrost": false,
  "forceBrownout": false,
  "forceSkip": false,
  "targetNode": "B"
} /*EDITMODE-END*/;

function TweaksUI({ tweaks, setTweak, onPing, lang, t }) {
  return (
    <TweaksPanel title={lang === "tr" ? "Ayarlar" : "Tweaks"}>
      <TweakSection label={lang === "tr" ? "Veri akışı" : "Data flow"}>
        <TweakSlider
          label={lang === "tr" ? "Paket aralığı" : "Packet interval"}
          value={tweaks.rateMs}
          onChange={(v) => setTweak("rateMs", v)}
          min={500} max={6000} step={100}
          unit="ms" />
        
        <TweakRadio
          label={lang === "tr" ? "Ping hedefi" : "Ping target"}
          value={tweaks.targetNode}
          onChange={(v) => setTweak("targetNode", v)}
          options={["A", "B"]} />
        
        <TweakButton
          label={lang === "tr" ? "Yeni paket gönder" : "Send packet"}
          onClick={onPing} />
        
      </TweakSection>

      <TweakSection label={lang === "tr" ? "Bayrak override" : "Flag override"}>
        <TweakToggle
          label={lang === "tr" ? "❄️ DON" : "❄️ Frost"}
          value={tweaks.forceFrost}
          onChange={(v) => setTweak("forceFrost", v)} />
        
        <TweakToggle
          label={lang === "tr" ? "⚡ DÜŞÜK GÜÇ" : "⚡ Brownout"}
          value={tweaks.forceBrownout}
          onChange={(v) => setTweak("forceBrownout", v)} />
        
        <TweakToggle
          label={lang === "tr" ? "↷ ATLAMA" : "↷ Node skip"}
          value={tweaks.forceSkip}
          onChange={(v) => setTweak("forceSkip", v)} />
        
      </TweakSection>
    </TweaksPanel>);

}

/* ================== App ================== */
function App() {
  const [lang, setLang] = useState("tr");
  const t = I18N[lang];

  // Başlangıçta boş veya null bırakıyoruz
  const [nodeA, setNodeA] = useState(null);
  const [nodeB, setNodeB] = useState(null);
  const [hist1, setHist1] = useState([]);
  const [hist2, setHist2] = useState([]);
  const [events, setEvents] = useState([]);
  const [lastEvent, setLastEvent] = useState(null);
  const [pulseA, setPulseA] = useState(0);
  const [pulseB, setPulseB] = useState(0);
  const [connected, setConnected] = useState(false);

  const evCounter = useRef(0);
  const prevFlagsA = useRef({ frost: false, brownout: false, node_skip: false });
  const prevFlagsB = useRef({ frost: false, brownout: false, node_skip: false });

  const pushEvent = useCallback((e) => {
    evCounter.current += 1;
    setEvents((prev) => [{ ...e, id: evCounter.current, at: Date.now() }, ...prev].slice(0, 60));
  }, []);

  const compareFlags = useCallback((oldF, newF, nodeName) => {
    const flagDefs = [
      { k: "frost", label: t.flagFrost },
      { k: "brownout", label: t.flagBrownout },
      { k: "node_skip", label: t.flagSkip }
    ];

    flagDefs.forEach((d) => {
      if (!oldF[d.k] && newF[d.k]) {
        pushEvent({ kind: "alert", title: `${nodeName}: ${t.flagRaised}`, detail: d.label });
      } else if (oldF[d.k] && !newF[d.k]) {
        pushEvent({ kind: "clear", title: `${nodeName}: ${t.flagCleared}`, detail: d.label });
      }
    });
  }, [pushEvent, t]);

  // WebSocket Bağlantısı Kurulumu
  useEffect(() => {
    // Backend sunucu adresimiz
    const socket = window.io();

    socket.on('connect', () => {
      setConnected(true);
      pushEvent({ kind: "pkt", title: "System", detail: lang === "tr" ? "Bağlantı kuruldu" : "Connected" });
    });

    socket.on('disconnect', () => {
      setConnected(false);
      pushEvent({ kind: "alert", title: "System", detail: lang === "tr" ? "Bağlantı koptu" : "Disconnected" });
    });

    socket.on('node_data', (payload) => {
      const id = ++evCounter.current;
      
      if (payload.node_id === 1) {
        setNodeA(prev => {
          if (prev) compareFlags(prev.flags || prevFlagsA.current, payload.flags, t.nodeA);
          return payload;
        });
        setHist1(prev => [...prev, { air_temp_c: payload.air_temp_c, air_humidity: payload.air_humidity }].slice(-10));
        setPulseA(p => p + 1);
        setLastEvent({ id, segment: "AB", toNode: "B", route: "A→B→GW" });
        pushEvent({ kind: "pkt", title: `${t.nodeA} → ${t.gateway}`, detail: t.pktReceived });
      } 
      else if (payload.node_id === 2) {
        setNodeB(prev => {
          if (prev) compareFlags(prev.flags || prevFlagsB.current, payload.flags, t.nodeB);
          return payload;
        });
        setHist2(prev => [...prev, { air_temp_c: payload.air_temp_c, air_humidity: payload.air_humidity }].slice(-10));
        setPulseB(p => p + 1);
        setLastEvent({ id, segment: "BG", toNode: "GW", route: "B→GW" });
        pushEvent({ kind: "pkt", title: `${t.nodeB} → ${t.gateway}`, detail: t.pktReceived });
      }
      
      // Çizgideki akan animasyonun bitmesi için zamanlayıcı
      setTimeout(() => setLastEvent(null), 1700);
    });

    return () => {
      socket.disconnect();
    };
  }, [lang, pushEvent, compareFlags, t]);

  // Geçmiş verilere göre grafikteki toprak nemi min/max değerlerinin hesaplanması
  const soilStatsA = useMemo(() => {
    if (!nodeA) return {};
    return { minSoil: Math.max(0, nodeA.soil_moisture - 6), maxSoil: Math.min(100, nodeA.soil_moisture + 5) };
  }, [nodeA]);
  
  const soilStatsB = useMemo(() => {
    if (!nodeB) return {};
    return { minSoil: Math.max(0, nodeB.soil_moisture - 5), maxSoil: Math.min(100, nodeB.soil_moisture + 6) };
  }, [nodeB]);

  const gatewayData = (nodeA || nodeB) ? {
    receivedAt: Math.max(nodeA?.receivedAt || 0, nodeB?.receivedAt || 0),
    rx_rssi_dbm: nodeB ? nodeB.rx_rssi_dbm : (nodeA ? nodeA.rx_rssi_dbm : 0),
    pktPerMin: 28
  } : null;

  return (
    <div style={{ minHeight: "100vh", background: "var(--bg-main)" }} data-screen-label="Dashboard">
      <Header t={t} connected={connected} lang={lang} setLang={setLang} />
      <main style={{ padding: 32, display: "flex", flexDirection: "column", gap: 32, maxWidth: 1440, margin: "0 auto" }}>
        <SummaryRow t={t} nodeA={nodeA} nodeB={nodeB} />
        <Topology t={t} lastEvent={lastEvent} />

        <div style={{ display: "flex", gap: 16 }}>
          <NodeCard data={nodeA ? { ...nodeA, ...soilStatsA } : null} name={t.nodeA} t={t} kind="edge" pulseKey={pulseA} />
          <NodeCard data={nodeB ? { ...nodeB, ...soilStatsB } : null} name={t.nodeB} t={t} kind="edge" pulseKey={pulseB} />
          <NodeCard data={gatewayData} name={t.gateway} t={t} kind="gateway" pulseKey={pulseA + pulseB} />
        </div>

        <div style={{ display: "flex", gap: 16, alignItems: "stretch" }}>
          <div style={{ flex: 1 }}>
            <SparklineSection t={t} hist1={hist1} hist2={hist2} />
          </div>
          <EventLog events={events} t={t} />
        </div>

        <footer style={{ textAlign: "center", fontSize: 11, color: "var(--text-secondary)", padding: "16px 0 24px" }}>
          Smart Agriculture IoT · Live WebSocket Feed · {new Date().toLocaleDateString()}
        </footer>
      </main>
    </div>
  );
}

ReactDOM.createRoot(document.getElementById("root")).render(<App />);
