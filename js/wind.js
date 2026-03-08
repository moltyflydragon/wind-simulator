// High-performance wind particle system using typed arrays
// Renders 10k+ particles without lag

const MAX_PARTICLES = 15000;

class WindParticleSystem {
  constructor(canvas) {
    this.canvas = canvas;
    // Struct of arrays for cache-friendly access
    this.x = new Float32Array(MAX_PARTICLES);
    this.y = new Float32Array(MAX_PARTICLES);
    this.vx = new Float32Array(MAX_PARTICLES);
    this.vy = new Float32Array(MAX_PARTICLES);
    this.life = new Float32Array(MAX_PARTICLES);
    this.count = 0;
    this.windSpeed = 30;
    this.windAngle = Math.PI * 1.5;
  }

  get windVelocity() {
    return Vec2.fromAngle(this.windAngle, this.windSpeed);
  }

  setWind(speed, angleDeg) {
    this.windSpeed = speed;
    this.windAngle = (angleDeg * Math.PI) / 180;
  }

  spawn(n) {
    const w = this.canvas.width;
    const h = this.canvas.height;
    const dx = Math.cos(this.windAngle);
    const dy = Math.sin(this.windAngle);
    const spd = this.windSpeed * 10;

    for (let i = 0; i < n && this.count < MAX_PARTICLES; i++) {
      const idx = this.count++;
      // Spawn from upwind edges
      if (Math.abs(dx) > Math.abs(dy)) {
        this.x[idx] = dx > 0 ? -5 : w + 5;
        this.y[idx] = Math.random() * h;
      } else {
        this.x[idx] = Math.random() * w;
        this.y[idx] = dy > 0 ? -5 : h + 5;
      }
      this.vx[idx] = dx * spd + (Math.random() - 0.5) * 30;
      this.vy[idx] = dy * spd + (Math.random() - 0.5) * 30;
      this.life[idx] = 0.6 + Math.random() * 0.4;
    }
  }

  update(dt, objects) {
    if (this.windSpeed < 0.5) { this.count = 0; return; }

    // Spawn proportional to wind speed - dense particle field
    this.spawn(Math.floor(this.windSpeed * 8));

    const w = this.canvas.width;
    const h = this.canvas.height;
    const windVx = Math.cos(this.windAngle) * this.windSpeed * 10;
    const windVy = Math.sin(this.windAngle) * this.windSpeed * 10;
    const objCount = objects.length;

    let alive = 0;
    for (let i = 0; i < this.count; i++) {
      let px = this.x[i];
      let py = this.y[i];
      let pvx = this.vx[i];
      let pvy = this.vy[i];
      let plife = this.life[i] - 0.004;

      if (plife <= 0 || px < -60 || px > w + 60 || py < -60 || py > h + 60) continue;

      // Deflect around objects
      for (let j = 0; j < objCount; j++) {
        const obj = objects[j];
        const ddx = px - obj.cx;
        const ddy = py - obj.cy;
        const distSq = ddx * ddx + ddy * ddy;
        const r = obj.boundingRadius * 1.8;

        if (distSq < r * r && distSq > 1) {
          const dist = Math.sqrt(distSq);
          const force = (r - dist) / r * 400 * dt;
          pvx += (ddx / dist) * force;
          pvy += (ddy / dist) * force;
        }
      }

      // Return toward wind direction
      pvx += (windVx - pvx) * 1.5 * dt;
      pvy += (windVy - pvy) * 1.5 * dt;

      px += pvx * dt;
      py += pvy * dt;

      // Compact alive particles (swap-to-front)
      this.x[alive] = px;
      this.y[alive] = py;
      this.vx[alive] = pvx;
      this.vy[alive] = pvy;
      this.life[alive] = plife;
      alive++;
    }
    this.count = alive;
  }

  render(ctx) {
    if (this.count === 0) return;

    // Batch render at a few alpha levels for speed (no per-particle color string)
    const buckets = [
      { min: 0.4, color: 'rgba(140,180,255,0.35)' },
      { min: 0.2, color: 'rgba(140,180,255,0.2)' },
      { min: 0.0, color: 'rgba(140,180,255,0.08)' }
    ];

    for (const b of buckets) {
      ctx.fillStyle = b.color;
      ctx.beginPath();
      for (let i = 0; i < this.count; i++) {
        if (this.life[i] >= b.min && (b.min === 0 || this.life[i] < b.min + 0.2)) {
          ctx.rect(this.x[i], this.y[i], 1.5, 1.5);
        }
      }
      ctx.fill();
    }
  }
}
