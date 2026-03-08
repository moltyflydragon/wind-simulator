class Vec2 {
  constructor(x = 0, y = 0) {
    this.x = x;
    this.y = y;
  }

  add(v) { return new Vec2(this.x + v.x, this.y + v.y); }
  sub(v) { return new Vec2(this.x - v.x, this.y - v.y); }
  scale(s) { return new Vec2(this.x * s, this.y * s); }
  dot(v) { return this.x * v.x + this.y * v.y; }
  mag() { return Math.sqrt(this.x * this.x + this.y * this.y); }

  normalize() {
    const m = this.mag();
    return m > 0 ? this.scale(1 / m) : new Vec2(0, 0);
  }

  rotate(angle) {
    const cos = Math.cos(angle);
    const sin = Math.sin(angle);
    return new Vec2(
      this.x * cos - this.y * sin,
      this.x * sin + this.y * cos
    );
  }

  clone() { return new Vec2(this.x, this.y); }

  static fromAngle(angle, mag = 1) {
    return new Vec2(Math.cos(angle) * mag, Math.sin(angle) * mag);
  }
}
