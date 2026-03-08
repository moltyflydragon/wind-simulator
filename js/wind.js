const MAX_PARTICLES = 15000;

class WindParticleSystem {
  constructor(canvas) {
    this.canvas = canvas;
    this.x = new Float32Array(MAX_PARTICLES);
    this.y = new Float32Array(MAX_PARTICLES);
    this.vx = new Float32Array(MAX_PARTICLES);
    this.vy = new Float32Array(MAX_PARTICLES);
    this.life = new Float32Array(MAX_PARTICLES);
    this.count = 0;
    this.windSpeed = 30;
    this.windAngle = Math.PI * 1.5;
    this.spawnAccum = 0; // fractional spawn accumulator
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
    const absDx = Math.abs(dx);
    const absDy = Math.abs(dy);

    for (let i = 0; i < n && this.count < MAX_PARTICLES; i++) {
      const idx = this.count++;

      // Spawn from upwind edges + random scatter across canvas
      if (Math.random() < 0.15) {
        // Some spawn randomly on canvas for density
        this.x[idx] = Math.random() * w;
        this.y[idx] = Math.random() * h;
      } else if (absDx > absDy) {
        this.x[idx] = dx > 0 ? Math.random() * -20 : w + Math.random() * 20;
        this.y[idx] = Math.random() * h;
      } else {
        this.x[idx] = Math.random() * w;
        this.y[idx] = dy > 0 ? Math.random() * -20 : h + Math.random() * 20;
      }

      this.vx[idx] = dx * spd + (Math.random() - 0.5) * 40;
      this.vy[idx] = dy * spd + (Math.random() - 0.5) * 40;
      this.life[idx] = 0.5 + Math.random() * 0.5;
    }
  }

  update(dt, objects) {
    if (this.windSpeed < 0.5) { this.count = 0; return; }

    // Spawn rate proportional to dt so it's frame-rate independent
    this.spawnAccum += this.windSpeed * 500 * dt;
    const toSpawn = Math.floor(this.spawnAccum);
    this.spawnAccum -= toSpawn;
    this.spawn(toSpawn);

    const w = this.canvas.width;
    const h = this.canvas.height;
    const windVx = Math.cos(this.windAngle) * this.windSpeed * 10;
    const windVy = Math.sin(this.windAngle) * this.windSpeed * 10;
    const objCount = objects.length;
    // Decay proportional to dt
    const decay = 0.25 * dt;

    let alive = 0;
    for (let i = 0; i < this.count; i++) {
      let px = this.x[i];
      let py = this.y[i];
      let pvx = this.vx[i];
      let pvy = this.vy[i];
      let plife = this.life[i] - decay;

      if (plife <= 0 || px < -80 || px > w + 80 || py < -80 || py > h + 80) continue;

      // Deflect around objects
      for (let j = 0; j < objCount; j++) {
        const obj = objects[j];
        const ddx = px - obj.cx;
        const ddy = py - obj.cy;
        const distSq = ddx * ddx + ddy * ddy;
        const r = obj.boundingRadius * 2.0;

        if (distSq < r * r && distSq > 1) {
          const dist = Math.sqrt(distSq);
          const force = ((r - dist) / r) * 500 * dt;
          pvx += (ddx / dist) * force;
          pvy += (ddy / dist) * force;
        }
      }

      // Steer back toward wind direction
      pvx += (windVx - pvx) * 2.0 * dt;
      pvy += (windVy - pvy) * 2.0 * dt;

      px += pvx * dt;
      py += pvy * dt;

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

    // Single pass, two alpha levels for slight depth
    ctx.fillStyle = 'rgba(150,190,255,0.3)';
    ctx.beginPath();
    for (let i = 0; i < this.count; i++) {
      if (this.life[i] > 0.25) {
        ctx.rect(this.x[i], this.y[i], 1.5, 1.5);
      }
    }
    ctx.fill();

    ctx.fillStyle = 'rgba(120,160,240,0.12)';
    ctx.beginPath();
    for (let i = 0; i < this.count; i++) {
      if (this.life[i] <= 0.25) {
        ctx.rect(this.x[i], this.y[i], 1, 1);
      }
    }
    ctx.fill();
  }
}
