const express = require('express');
const cors = require('cors');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname)));

const ARM_URL = (process.env.ARM_URL || 'http://localhost:8000').replace(/\/$/, '');
const ARM_POLL_MS = 400;
let armLink = { online: false, simulated: null, lastError: null, checkedAt: null };

let telemetryHistory = [];
const MAX_HISTORY = 100;

let seqCounter = 1;
let startTime = Date.now();
let cleanState = {
  panelB_cleaned_zones: new Set(),
  panelC_cleaned_zones: new Set(),
  armStatus: 'IDLE',
  armTarget: null,
  lastCleanedTimestamp: null
};

let simConfig = {
  sunlightLux: 84500.0,
  ambientTemp: 28.5,
  humidity: 52.0,
  rainRate: 0.0,
  rainTotal: 12.4,
  cloudShading: false,
  thermalLossMode: false,
  soilingLevelB: 0.286,
  soilingLevelC: 0.128,
  simulatedMode: true
};

function generateZoneData(panelKey, basePower, lossRatio, cleanedZones) {
  const zones = [];
  const rows = 3;
  const cols = 4;
  const zoneWidth = 1.6 / cols;
  const zoneHeight = 1.0 / rows;

  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const zoneIndex = r * cols + c + 1;
      const zoneId = `Z${zoneIndex.toString().padStart(2, '0')}`;
      const posX = Number((c * zoneWidth + zoneWidth / 2).toFixed(2));
      const posY = Number((r * zoneHeight + zoneHeight / 2).toFixed(2));

      let zoneLoss = 0;
      let status = 'CLEAN';

      if (panelKey === 'panelA') {
        zoneLoss = (Math.sin(zoneIndex * 1.5) * 0.01);
        status = 'CLEAN';
      } else {
        const isCleaned = cleanedZones.has(zoneId);
        if (isCleaned) {
          zoneLoss = 0.015 + (Math.random() * 0.01);
          status = 'VERIFIED_CLEAN';
        } else {
          let dustFactor = 1.0;
          if (zoneId === 'Z03') dustFactor = 1.8;
          if (zoneId === 'Z07') dustFactor = 1.4;
          if (zoneId === 'Z04') dustFactor = 1.3;

          zoneLoss = Math.min(0.45, lossRatio * dustFactor);

          if (simConfig.thermalLossMode) {
            status = 'HOTSPOT';
          } else if (zoneLoss > 0.22) {
            status = 'HEAVY_SOILING';
          } else if (zoneLoss > 0.10) {
            status = 'LIGHT_DUST';
          }
        }
      }

      const zoneBaselinePower = basePower / 12;
      const zonePower = Math.max(0, zoneBaselinePower * (1 - zoneLoss));

      zones.push({
        id: zoneId,
        col: c + 1,
        row: r + 1,
        x: posX,
        y: posY,
        loss_pct: Number((zoneLoss * 100).toFixed(1)),
        power_w: Number(zonePower.toFixed(2)),
        baseline_w: Number(zoneBaselinePower.toFixed(2)),
        status: status
      });
    }
  }

  return zones;
}

function createTelemetryRecord() {
  const now = new Date();
  const uptimeMs = Date.now() - startTime;

  let lux = simConfig.sunlightLux + (Math.random() * 1200 - 600);
  if (simConfig.cloudShading) {
    lux = 14200.0 + (Math.random() * 800);
  }

  let lightStatus = 'OK';
  if (lux >= 120000.0) lightStatus = 'SATURATED';
  else if (lux < 20000.0) lightStatus = 'LOW_LIGHT';

  const lightRatio = Math.max(0.05, Math.min(1.2, lux / 85000.0));
  const panelA_v = 18.42 + (Math.random() * 0.08 - 0.04);
  const panelA_i = Number((1.24 * lightRatio + (Math.random() * 0.01)).toFixed(3));
  const panelA_p = Number((panelA_v * panelA_i).toFixed(2));
  const panelA_temp = Number((simConfig.ambientTemp + (panelA_p * 0.4)).toFixed(1));

  const effectiveLossB = Array.from(cleanState.panelB_cleaned_zones).length > 0
    ? Math.max(0.02, simConfig.soilingLevelB * (1 - (cleanState.panelB_cleaned_zones.size / 12)))
    : simConfig.soilingLevelB;

  const panelB_v = 17.91 + (Math.random() * 0.06 - 0.03);
  const panelB_i = Number((panelA_i * (1 - effectiveLossB)).toFixed(3));
  const panelB_p = Number((panelB_v * panelB_i).toFixed(2));
  const panelB_temp = Number((simConfig.ambientTemp + (panelB_p * 0.4) + (effectiveLossB * 3.0)).toFixed(1));

  const effectiveLossC = Array.from(cleanState.panelC_cleaned_zones).length > 0
    ? Math.max(0.015, simConfig.soilingLevelC * (1 - (cleanState.panelC_cleaned_zones.size / 12)))
    : simConfig.soilingLevelC;

  const panelC_v = 18.10 + (Math.random() * 0.05 - 0.025);
  const panelC_i = Number((panelA_i * (1 - effectiveLossC)).toFixed(3));
  const panelC_p = Number((panelC_v * panelC_i).toFixed(2));
  const panelC_temp = Number((simConfig.ambientTemp + (panelC_p * 0.4)).toFixed(1));

  let validComp = true;
  let compNote = 'valid';
  let effB = null;
  let effC = null;
  let lossB_pct = null;
  let lossC_pct = null;

  if (panelA_p < 0.5) {
    validComp = false;
    compNote = 'reference output too low to compare';
  } else if (lux < 20000.0) {
    validComp = false;
    compNote = 'low sunlight - difference not attributable to soiling';
  } else {
    effB = Number((panelB_p / panelA_p).toFixed(4));
    effC = Number((panelC_p / panelA_p).toFixed(4));
    lossB_pct = Number(((1 - effB) * 100).toFixed(2));
    lossC_pct = Number(((1 - effC) * 100).toFixed(2));
  }

  const zonesA = generateZoneData('panelA', panelA_p, 0, new Set());
  const zonesB = generateZoneData('panelB', panelA_p, effectiveLossB, cleanState.panelB_cleaned_zones);
  const zonesC = generateZoneData('panelC', panelA_p, effectiveLossC, cleanState.panelC_cleaned_zones);

  let targetZone = null;
  let maxLoss = 0;
  zonesB.forEach(z => {
    if (z.loss_pct > maxLoss && z.status !== 'VERIFIED_CLEAN') {
      maxLoss = z.loss_pct;
      targetZone = { panel: 'panelB', panelName: 'Panel B (Uncleaned)', ...z };
    }
  });

  let recommendation = 'CLEAN_NOW';
  let recNote = 'Heavy soiling anomaly confirmed across 4 consecutive cycles.';
  let rciScore = 0.88;

  if (simConfig.rainRate > 5.0) {
    recommendation = 'WAIT_FOR_RAIN';
    recNote = 'Active heavy rainfall cleans panels naturally. Cleaning arm paused.';
    rciScore = 0.95;
  } else if (simConfig.rainRate > 0.1 && simConfig.rainRate <= 2.0 && simConfig.humidity > 80) {
    recommendation = 'WARN_RAIN_SOILING';
    recNote = 'Light drizzle + high humidity will turn dust into sticky mud paste. Clean immediately!';
    rciScore = 0.32;
  } else if (lux < 20000.0) {
    recommendation = 'WAIT_LOW_LIGHT';
    recNote = 'Low solar irradiance. Cannot separate cloud shading from soiling loss.';
  } else if (simConfig.thermalLossMode) {
    recommendation = 'DO_NOT_ACT_THERMAL';
    recNote = 'High temperature thermal efficiency loss detected. Do not actuate brush.';
  }

  const record = {
    device_id: 'SOLARSENSE_01',
    seq: seqCounter++,
    timestamp: now.toISOString().replace('T', ' ').substring(0, 19),
    time_source: 'NTP',
    uptime_ms: uptimeMs,
    light_lux: Number(lux.toFixed(1)),
    light_status: lightStatus,
    panelA: {
      voltage: panelA_v,
      current: panelA_i,
      power: panelA_p,
      temperature: panelA_temp,
      status: 'OK',
      temperature_status: 'OK',
      zones: zonesA
    },
    panelB: {
      voltage: panelB_v,
      current: panelB_i,
      power: panelB_p,
      temperature: panelB_temp,
      status: 'OK',
      temperature_status: 'OK',
      zones: zonesB
    },
    panelC: {
      voltage: panelC_v,
      current: panelC_i,
      power: panelC_p,
      temperature: panelC_temp,
      status: 'OK',
      temperature_status: 'OK',
      zones: zonesC
    },
    ambient: {
      temperature: simConfig.ambientTemp,
      humidity: simConfig.humidity,
      rainfall_mm: simConfig.rainRate,
      rainfall_total_mm: simConfig.rainTotal,
      status: 'OK'
    },
    comparison: {
      valid: validComp,
      note: compNote,
      efficiency_B: effB,
      efficiency_C: effC,
      loss_B_pct: lossB_pct,
      loss_C_pct: lossC_pct
    },
    ai_decision: {
      recommendation: recommendation,
      note: recNote,
      rci_score: rciScore,
      target_zone: targetZone,
      robotic_arm: {
        link: armLink.online ? 'CONNECTED' : 'OFFLINE',
        status: cleanState.armStatus,
        target: cleanState.armTarget,
        last_cleaned: cleanState.lastCleanedTimestamp
      }
    },
    firmware: '0.5.0',
    simulated: simConfig.simulatedMode
  };

  telemetryHistory.push(record);
  if (telemetryHistory.length > MAX_HISTORY) {
    telemetryHistory.shift();
  }

  return record;
}

setInterval(() => {
  createTelemetryRecord();
}, 3000);

for (let i = 0; i < 10; i++) {
  createTelemetryRecord();
}

app.post('/api/sensors/data', (req, res) => {
  const data = req.body;
  if (!data || !data.device_id) {
    return res.status(400).json({ error: 'Invalid payload schema' });
  }

  data.received_at = new Date().toISOString();
  telemetryHistory.push(data);
  if (telemetryHistory.length > MAX_HISTORY) telemetryHistory.shift();

  res.status(201).json({ status: 'ACCEPTED', seq: data.seq, committed: true });
});

app.get('/api/sensors/latest', (req, res) => {
  const latest = telemetryHistory[telemetryHistory.length - 1] || createTelemetryRecord();
  res.json(latest);
});

app.get('/api/sensors/history', (req, res) => {
  res.json(telemetryHistory);
});


async function armRequest(method, route, body) {
  const res = await fetch(ARM_URL + route, {
    method,
    headers: { 'Content-Type': 'application/json' },
    body: body ? JSON.stringify(body) : undefined,
    signal: AbortSignal.timeout(2500)
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.detail || `arm controller returned HTTP ${res.status}`);
  return data;
}

function markZoneCleaned(panel, zoneId) {
  if (panel === 'panelB') cleanState.panelB_cleaned_zones.add(zoneId);
  if (panel === 'panelC') cleanState.panelC_cleaned_zones.add(zoneId);
}

function runLocalCleaningTimeline(panel, zoneId) {
  setTimeout(() => { cleanState.armStatus = 'CLEANING'; }, 1500);
  setTimeout(() => {
    cleanState.armStatus = 'VERIFYING';
    markZoneCleaned(panel, zoneId);
  }, 3500);
  setTimeout(() => {
    cleanState.armStatus = 'IDLE';
    cleanState.lastCleanedTimestamp = new Date().toISOString();
  }, 5000);
}

function followArmJob(panel, zoneId) {
  const started = Date.now();
  const timer = setInterval(async () => {
    try {
      const job = await armRequest('GET', '/api/routine/clean_status');
      armLink = { online: true, simulated: job.simulated, lastError: null, checkedAt: new Date().toISOString() };
      if (job.state === 'VERIFYING') markZoneCleaned(panel, zoneId);
      if (job.state === 'IDLE') {
        clearInterval(timer);
        cleanState.armStatus = 'IDLE';
        if (job.error) {
          armLink.lastError = job.error;
        } else {
          markZoneCleaned(panel, zoneId);
          cleanState.lastCleanedTimestamp = new Date().toISOString();
        }
        return;
      }
      cleanState.armStatus = job.state;
    } catch (err) {
      armLink.lastError = err.message;
    }
    if (Date.now() - started > 60000) {
      clearInterval(timer);
      cleanState.armStatus = 'IDLE';
      armLink.lastError = 'arm did not report back within 60 s';
    }
  }, ARM_POLL_MS);
}

app.get('/api/arm/status', async (req, res) => {
  try {
    const job = await armRequest('GET', '/api/routine/clean_status');
    armLink = { online: true, simulated: job.simulated, lastError: armLink.lastError, checkedAt: new Date().toISOString() };
    res.json({ ...armLink, url: ARM_URL, job });
  } catch (err) {
    armLink = { online: false, simulated: null, lastError: err.message, checkedAt: new Date().toISOString() };
    res.json({ ...armLink, url: ARM_URL, job: null });
  }
});

app.post('/api/actuate/clean', async (req, res) => {
  const { panel, zoneId, x, y } = req.body;
  const targetPanel = panel || 'panelB';
  const targetZoneId = zoneId || 'Z03';
  const targetX = x !== undefined ? x : 0.82;
  const targetY = y !== undefined ? y : 0.12;

  if (cleanState.armStatus !== 'IDLE') {
    return res.status(409).json({ success: false, message: `Arm is busy (${cleanState.armStatus})` });
  }

  cleanState.armStatus = 'MOVING';
  cleanState.armTarget = { panel: targetPanel, zoneId: targetZoneId, x: targetX, y: targetY };

  let mode = 'LOCAL_SIMULATION';
  try {
    await armRequest('POST', '/api/routine/clean_zone', { panel: targetPanel, zone_id: targetZoneId, x: targetX, y: targetY });
    armLink = { online: true, simulated: null, lastError: null, checkedAt: new Date().toISOString() };
    mode = 'ROBOTIC_ARM';
    followArmJob(targetPanel, targetZoneId);
  } catch (err) {
    armLink = { online: false, simulated: null, lastError: err.message, checkedAt: new Date().toISOString() };
    runLocalCleaningTimeline(targetPanel, targetZoneId);
  }

  res.json({
    success: true,
    mode,
    message: mode === 'ROBOTIC_ARM'
      ? `Robotic arm dispatched to ${targetPanel} ${targetZoneId} (X: ${targetX}m, Y: ${targetY}m)`
      : `Arm controller offline, running simulated clean of ${targetPanel} ${targetZoneId}`,
    armTarget: cleanState.armTarget
  });
});

app.post('/api/simulate/config', (req, res) => {
  if (req.body.resetCleaned) {
    cleanState.panelB_cleaned_zones.clear();
    cleanState.panelC_cleaned_zones.clear();
  }
  if (req.body.cloudShading !== undefined) simConfig.cloudShading = req.body.cloudShading;
  if (req.body.thermalLossMode !== undefined) simConfig.thermalLossMode = req.body.thermalLossMode;
  if (req.body.rainRate !== undefined) simConfig.rainRate = req.body.rainRate;
  if (req.body.soilingLevelB !== undefined) simConfig.soilingLevelB = req.body.soilingLevelB;

  res.json({ success: true, config: simConfig, cleanState });
});

async function checkArmLink() {
  try {
    const job = await armRequest('GET', '/api/routine/clean_status');
    armLink = { online: true, simulated: job.simulated, lastError: armLink.lastError, checkedAt: new Date().toISOString() };
  } catch (err) {
    armLink = { online: false, simulated: null, lastError: err.message, checkedAt: new Date().toISOString() };
  }
}

checkArmLink();
setInterval(checkArmLink, 5000);

app.listen(PORT, () => {
  console.log(`SolarSense Dashboard Backend running on http://localhost:${PORT}`);
  console.log(`Robotic arm controller expected at ${ARM_URL}`);
});
