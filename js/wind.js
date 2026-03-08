class WindParticleSystem {
  constructor(canvas) {
    this.canvas = canvas;
    this.particles = [];
    this.maxParticles = 800;
    this.windSpeed = 30;
    this.windAngle = Math.PI * 1.5; // 270 deg = blowing left to right upward
    this.enabled = true;
  }

  get windVelocity() {
    return Vec2.fromAngle(this.windAngle, this.windSpeed);
  }

  setWind(speed, angleDeg) {
    this.windSpeed = speed;
    this.windAngle = (angleDeg * Math.PI) / 180;
  }

  spawnParticle() {
    const w = this.canvas.width;
    const h = this.canvas.height;
    const dir = this.windVelocity.normalize();

    // Spawn from edges opposite to wind direction
    let x, y;
    if (Math.abs(dir.x) > Math.abs(dir.y)) {
      x = dir.x > 0 ? -10 : w + 10;
      y = Math.random() * h;
    } else {
      x = Math.random() * w;
      y = dir.y > 0 ? -10 : h + 10;
    }

    return {
      x, y,
      vx: dir.x * this.windSpeed * 10 + (Math.random() - 0.5) * 20,
      vy: dir.y * this.windSpeed * 10 + (Math.random() - 0.5) * 20,
      life: 1.0,
      decay: 0.002 + Math.random() * 0.003,
      size: 0.5 + Math.random() * 1.0
    };
  }

  update(dt, objects) {
    if (!this.enabled || this.windSpeed < 0.5) {
      this.particles = [];
      return;
    }

    // Spawn new particles
    const spawnRate = Math.floor(this.windSpeed * 1.5);
    for (let i = 0; i < spawnRate && this.particles.length < this.maxParticles; i++) {
      this.particles.push(this.spawnParticle());
    }

    const w = this.canvas.width;
    const h = this.canvas.height;

    // Update existing particles
    for (let i = this.particles.length - 1; i >= 0; i--) {
      const p = this.particles[i];

      // Deflect around objects
      for (const obj of objects) {
        const dx = p.x - obj.cx;
        const dy = p.y - obj.cy;
        const dist = Math.sqrt(dx * dx + dy * dy);
        const radius = obj.boundingRadius || 30;

        if (dist < radius * 1.5 && dist > 1) {
          // Push particle around the object
          const force = (radius * 1.5 - dist) / (radius * 1.5);
          p.vx += (dx / dist) * force * 200 * dt;
          p.vy += (dy / dist) * force * 200 * dt;
        }
      }

      p.x += p.vx * dt;
      p.y += p.vy * dt;

      // Gradually return to wind direction
      const windVx = this.windVelocity.x * 10;
      const windVy = this.windVelocity.y * 10;
      p.vx += (windVx - p.vx) * 2 * dt;
      p.vy += (windVy - p.vy) * 2 * dt;

      p.life -= p.decay;

      // Remove dead or out-of-bounds particles
      if (p.life <= 0 || p.x < -50 || p.x > w + 50 || p.y < -50 || p.y > h + 50) {
        this.particles.splice(i, 1);
      }
    }
  }

  render(ctx) {
    if (!this.enabled) return;

    for (const p of this.particles) {
      const alpha = p.life * 0.6;
      ctx.fillStyle = `rgba(140, 180, 255, ${alpha})`;
      ctx.beginPath();
      ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
      ctx.fill();
    }
  }
}
