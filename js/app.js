// -- State --
const objects = [];
let simRunning = false;
let lastTime = null;

// -- Canvas setup --
const canvas = document.getElementById('sim-canvas');
const ctx = canvas.getContext('2d');

function resizeCanvas() {
  const workspace = document.getElementById('workspace');
  canvas.width = workspace.clientWidth - 220; // minus meta panel
  canvas.height = workspace.clientHeight;
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

// -- Systems --
const physics = new PhysicsEngine();
const wind = new WindParticleSystem(canvas);
const drawing = new DrawingManager(canvas, objects);

drawing.onObjectCreated = (obj) => {
  physics.addBody(obj);
  updateMetaPanel();
};

drawing.onObjectDeleted = (obj) => {
  physics.removeBody(obj);
  updateMetaPanel();
};

// -- Toolbar wiring --
const toolButtons = {
  'btn-select': 'select',
  'btn-pencil': 'pencil',
  'btn-line': 'line',
  'btn-rect': 'rect',
  'btn-circle': 'circle',
  'btn-stick': 'stick'
};

Object.entries(toolButtons).forEach(([btnId, tool]) => {
  document.getElementById(btnId).addEventListener('click', () => {
    drawing.currentTool = tool;
    document.querySelectorAll('.tool-group:first-child .tool-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(btnId).classList.add('active');
  });
});

document.getElementById('btn-play').addEventListener('click', () => {
  if (!simRunning) {
    // Save origins before starting
    for (const obj of objects) obj.saveOrigin();
    simRunning = true;
    lastTime = performance.now();
  }
});

document.getElementById('btn-pause').addEventListener('click', () => {
  simRunning = false;
});

document.getElementById('btn-reset').addEventListener('click', () => {
  simRunning = false;
  physics.reset();
  for (const obj of objects) obj.resetPosition();
});

// Wind controls
const windSpeedSlider = document.getElementById('wind-speed');
const windDirSlider = document.getElementById('wind-dir');
const windSpeedVal = document.getElementById('wind-speed-val');
const windDirVal = document.getElementById('wind-dir-val');

windSpeedSlider.addEventListener('input', () => {
  const speed = Number(windSpeedSlider.value);
  windSpeedVal.textContent = `${speed} m/s`;
  wind.setWind(speed, Number(windDirSlider.value));
});

windDirSlider.addEventListener('input', () => {
  const dir = Number(windDirSlider.value);
  windDirVal.textContent = `${dir}deg`;
  wind.setWind(Number(windSpeedSlider.value), dir);
});

// Transform controls
document.getElementById('btn-rot-ccw').addEventListener('click', () => {
  if (drawing.selectedObject) {
    drawing.selectedObject.rotate(-5 * Math.PI / 180);
    updateMetaPanel();
  }
});

document.getElementById('btn-rot-cw').addEventListener('click', () => {
  if (drawing.selectedObject) {
    drawing.selectedObject.rotate(5 * Math.PI / 180);
    updateMetaPanel();
  }
});

document.getElementById('btn-flip-h').addEventListener('click', () => {
  if (drawing.selectedObject) {
    drawing.selectedObject.flipHorizontal();
    updateMetaPanel();
  }
});

document.getElementById('btn-delete').addEventListener('click', () => {
  if (drawing.selectedObject) {
    physics.removeBody(drawing.selectedObject);
    const idx = objects.indexOf(drawing.selectedObject);
    if (idx !== -1) objects.splice(idx, 1);
    drawing.selectObject(null);
    updateMetaPanel();
  }
});

// -- Metadata panel --
function updateMetaPanel() {
  const panel = document.getElementById('meta-content');
  const obj = drawing.selectedObject;

  if (!obj) {
    panel.innerHTML = '<p class="placeholder">Select an object to see its properties</p>';
    return;
  }

  const body = physics.getBody(obj);
  const angleDeg = ((obj.angle * 180 / Math.PI) % 360).toFixed(1);
  const speed = body ? body.velocity.mag().toFixed(1) : '0.0';
  const relWind = body ? body.relativeWindSpeed.toFixed(1) : '0.0';
  const relWindAngle = body ? (body.relativeWindAngle * 180 / Math.PI).toFixed(1) : '0.0';
  const termV = body ? body.terminalVelocity.toFixed(1) : '-';
  const dragF = body ? body.dragForce.mag().toFixed(1) : '0.0';
  const liftF = body ? body.liftForce.mag().toFixed(1) : '0.0';

  panel.innerHTML = `
    <div class="meta-row"><span class="meta-label">Type</span><span class="meta-value">${obj.type}</span></div>
    <div class="meta-row"><span class="meta-label">ID</span><span class="meta-value">#${obj.id}</span></div>
    <div class="meta-row"><span class="meta-label">Angle</span><span class="meta-value">${angleDeg}deg</span></div>
    <div class="meta-row"><span class="meta-label">Position</span><span class="meta-value">${obj.cx.toFixed(0)}, ${obj.cy.toFixed(0)}</span></div>
    <div class="meta-row"><span class="meta-label">Pinned</span><span class="meta-value">${obj.pinned ? 'YES' : 'NO'}</span></div>

    <div class="meta-section">
      <div class="meta-section-title">Physics</div>
      <div class="meta-row"><span class="meta-label">Mass</span><span class="meta-value">${obj.mass} kg</span></div>
      <div class="meta-row"><span class="meta-label">Cd</span><span class="meta-value">${obj.dragCoefficient}</span></div>
      <div class="meta-row"><span class="meta-label">Area</span><span class="meta-value">${obj.crossSection} m2</span></div>
    </div>

    <div class="meta-section">
      <div class="meta-section-title">Live Data</div>
      <div class="meta-row"><span class="meta-label">Velocity</span><span class="meta-value">${speed} m/s</span></div>
      <div class="meta-row"><span class="meta-label">Rel. Wind</span><span class="meta-value">${relWind} m/s</span></div>
      <div class="meta-row"><span class="meta-label">AoA</span><span class="meta-value">${relWindAngle}deg</span></div>
      <div class="meta-row"><span class="meta-label">Terminal V</span><span class="meta-value">${termV} m/s</span></div>
      <div class="meta-row"><span class="meta-label">Drag Force</span><span class="meta-value">${dragF} N</span></div>
      <div class="meta-row"><span class="meta-label">Lift Force</span><span class="meta-value">${liftF} N</span></div>
    </div>
  `;
}

// -- Render loop --
function render(now) {
  requestAnimationFrame(render);

  const dt = simRunning && lastTime ? Math.min((now - lastTime) / 1000, 0.05) : 0;
  lastTime = now;

  if (simRunning) {
    physics.update(dt, wind.windVelocity);
  }

  wind.update(1 / 60, objects);

  // Clear
  ctx.fillStyle = '#0d0d14';
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  // Grid
  ctx.strokeStyle = 'rgba(255,255,255,0.03)';
  ctx.lineWidth = 1;
  for (let x = 0; x < canvas.width; x += 50) {
    ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.height); ctx.stroke();
  }
  for (let y = 0; y < canvas.height; y += 50) {
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke();
  }

  // Wind particles
  wind.render(ctx);

  // Objects
  for (const obj of objects) {
    obj.render(ctx);
  }

  // Drawing preview
  drawing.renderPreview();

  // HUD
  ctx.fillStyle = 'rgba(100, 120, 200, 0.5)';
  ctx.font = '11px Courier New';
  ctx.fillText(`Wind: ${windSpeedSlider.value} m/s @ ${windDirSlider.value}deg`, 10, canvas.height - 30);
  ctx.fillText(`Objects: ${objects.length} | Sim: ${simRunning ? 'RUNNING' : 'PAUSED'}`, 10, canvas.height - 14);

  // Update meta panel during sim
  if (simRunning && drawing.selectedObject) {
    updateMetaPanel();
  }
}

requestAnimationFrame(render);
