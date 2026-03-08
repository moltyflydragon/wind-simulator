const objects = [];
let simRunning = false;
let lastTime = null;

const canvas = document.getElementById('sim-canvas');
const ctx = canvas.getContext('2d');

function resizeCanvas() {
  const workspace = document.getElementById('workspace');
  canvas.width = workspace.clientWidth - 220;
  canvas.height = workspace.clientHeight;
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

const physics = new PhysicsEngine();
const wind = new WindParticleSystem(canvas);
const drawing = new DrawingManager(canvas, objects);

drawing.onObjectCreated = (obj) => { physics.addBody(obj); updateMeta(); };
drawing.onObjectDeleted = (obj) => { physics.removeBody(obj); updateMeta(); };

// Tool buttons
const tools = { 'btn-select':'select','btn-pencil':'pencil','btn-line':'line','btn-rect':'rect','btn-circle':'circle','btn-stick':'stick' };
Object.entries(tools).forEach(([id, tool]) => {
  document.getElementById(id).addEventListener('click', () => {
    drawing.currentTool = tool;
    Object.keys(tools).forEach(k => document.getElementById(k).classList.remove('active'));
    document.getElementById(id).classList.add('active');
  });
});

// Play/Pause toggle
const toggleBtn = document.getElementById('btn-toggle-sim');
toggleBtn.addEventListener('click', () => {
  simRunning = !simRunning;
  if (simRunning) {
    for (const obj of objects) obj.saveOrigin();
    lastTime = performance.now();
    toggleBtn.textContent = 'PAUSE';
    toggleBtn.classList.add('active');
  } else {
    toggleBtn.textContent = 'PLAY';
    toggleBtn.classList.remove('active');
  }
});

document.getElementById('btn-reset').addEventListener('click', () => {
  simRunning = false;
  toggleBtn.textContent = 'PLAY';
  toggleBtn.classList.remove('active');
  physics.reset();
  for (const obj of objects) obj.resetPosition();
});

// Wind
const windSpeedSlider = document.getElementById('wind-speed');
const windDirSlider = document.getElementById('wind-dir');
const windSpeedVal = document.getElementById('wind-speed-val');
const windDirVal = document.getElementById('wind-dir-val');

windSpeedSlider.addEventListener('input', () => {
  windSpeedVal.textContent = windSpeedSlider.value + ' m/s';
  wind.setWind(+windSpeedSlider.value, +windDirSlider.value);
});
windDirSlider.addEventListener('input', () => {
  windDirVal.textContent = windDirSlider.value;
  wind.setWind(+windSpeedSlider.value, +windDirSlider.value);
});

// Transform
document.getElementById('btn-rot-ccw').addEventListener('click', () => {
  if (drawing.selectedObject) { drawing.selectedObject.rotate(-5 * Math.PI / 180); updateMeta(); }
});
document.getElementById('btn-rot-cw').addEventListener('click', () => {
  if (drawing.selectedObject) { drawing.selectedObject.rotate(5 * Math.PI / 180); updateMeta(); }
});
document.getElementById('btn-delete').addEventListener('click', () => {
  if (drawing.selectedObject) {
    physics.removeBody(drawing.selectedObject);
    const idx = objects.indexOf(drawing.selectedObject);
    if (idx !== -1) objects.splice(idx, 1);
    drawing.selectObject(null);
    updateMeta();
  }
});

// Metadata panel
function updateMeta() {
  const panel = document.getElementById('meta-content');
  const obj = drawing.selectedObject;
  if (!obj) { panel.innerHTML = '<p class="placeholder">Select an object</p>'; return; }

  const body = physics.getBody(obj);
  const deg = ((obj.angle * 180 / Math.PI) % 360).toFixed(1);
  const mps = body ? body.speedMps.toFixed(1) : '0.0';
  const mph = body ? body.speedMph.toFixed(0) : '0';
  const relW = body ? body.relativeWindSpeed.toFixed(1) : '0.0';
  const aoa = body ? (body.relativeWindAngle * 180 / Math.PI).toFixed(1) : '0.0';
  const tv = body ? body.terminalVelocity.toFixed(1) : '-';
  const tvMph = body ? (body.terminalVelocity * 2.237).toFixed(0) : '-';
  const alt = body ? body.altitude.toFixed(0) : '-';
  const altFt = body ? (body.altitude * 3.281).toFixed(0) : '-';
  const rho = body ? body.airDensity.toFixed(3) : '-';
  const cfg = body ? (BODY_CONFIGS[body.bodyConfig]?.label || 'Custom') : '-';

  panel.innerHTML = `
    <div class="meta-row"><span class="meta-label">Type</span><span class="meta-value">${obj.type}</span></div>
    <div class="meta-row"><span class="meta-label">Angle</span><span class="meta-value">${deg} deg</span></div>
    <div class="meta-row"><span class="meta-label">Altitude</span><span class="meta-value">${alt}m / ${altFt}ft</span></div>
    <div class="meta-row"><span class="meta-label">Air Density</span><span class="meta-value">${rho} kg/m3</span></div>
    <div class="meta-section">
      <div class="meta-section-title">Speed</div>
      <div class="meta-row"><span class="meta-label">Velocity</span><span class="meta-value">${mps} m/s (${mph} mph)</span></div>
      <div class="meta-row"><span class="meta-label">Terminal V</span><span class="meta-value">${tv} m/s (${tvMph} mph)</span></div>
      <div class="meta-row"><span class="meta-label">Rel Wind</span><span class="meta-value">${relW} m/s</span></div>
      <div class="meta-row"><span class="meta-label">AoA</span><span class="meta-value">${aoa} deg</span></div>
    </div>
    <div class="meta-section">
      <div class="meta-section-title">Config</div>
      <div class="meta-row"><span class="meta-label">Position</span><span class="meta-value">${cfg}</span></div>
      <div class="meta-row"><span class="meta-label">Mass</span><span class="meta-value">${obj.mass} kg</span></div>
      <div class="meta-row"><span class="meta-label">Cd / Area</span><span class="meta-value">${obj.dragCoefficient} / ${obj.crossSection} m2</span></div>
      <select id="meta-body-config" style="width:100%;margin-top:4px;background:#1a1a2a;color:#aab;border:1px solid #333;padding:3px;font-family:inherit;font-size:11px;">
        ${Object.entries(BODY_CONFIGS).map(([k, v]) =>
          `<option value="${k}" ${body && body.bodyConfig === k ? 'selected' : ''}>${v.label} (Cd:${v.cd})</option>`
        ).join('')}
      </select>
    </div>
  `;

  const sel = document.getElementById('meta-body-config');
  if (sel && body) sel.addEventListener('change', () => { body.setBodyConfig(sel.value); updateMeta(); });
}

// Render loop
function render(now) {
  requestAnimationFrame(render);

  const dt = simRunning && lastTime ? Math.min((now - lastTime) / 1000, 0.05) : 0;
  lastTime = now;

  if (simRunning) physics.update(dt, wind.windVelocity);
  wind.update(1 / 60, objects);

  ctx.fillStyle = '#0d0d14';
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  // Grid
  ctx.strokeStyle = 'rgba(255,255,255,0.03)';
  ctx.lineWidth = 1;
  for (let x = 0; x < canvas.width; x += 50) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.height); ctx.stroke(); }
  for (let y = 0; y < canvas.height; y += 50) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke(); }

  wind.render(ctx);
  for (const obj of objects) obj.render(ctx);
  drawing.renderPreview();

  // HUD
  ctx.fillStyle = 'rgba(100,120,200,0.5)';
  ctx.font = '11px Courier New';
  ctx.fillText(`Wind: ${windSpeedSlider.value} m/s @ ${windDirSlider.value}deg | Particles: ${wind.count}`, 10, canvas.height - 30);
  ctx.fillText(`Objects: ${objects.length} | ${simRunning ? 'RUNNING' : 'PAUSED'}`, 10, canvas.height - 14);

  if (drawing.selectedObject) {
    const b = physics.getBody(drawing.selectedObject);
    if (b) ctx.fillText(`Speed: ${b.speedMph.toFixed(0)} mph | Alt: ${b.altitude.toFixed(0)}m`, 10, canvas.height - 46);
  }

  if (simRunning && drawing.selectedObject) updateMeta();
}

requestAnimationFrame(render);
