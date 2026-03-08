let objectIdCounter = 0;

class SimObject {
  constructor(type, cx, cy) {
    this.id = ++objectIdCounter;
    this.type = type; // 'pencil' or 'stick'
    this.cx = cx;
    this.cy = cy;
    this.angle = 0;
    this.points = []; // for pencil strokes (relative to cx,cy)
    this.height = 0;
    this.boundingRadius = 30;
    this.pinned = false;
    this.mass = 80;
    this.dragCoefficient = 0.7;
    this.crossSection = 0.7;
    this.color = '#e0e0f0';
    this.selected = false;
    this.originCx = cx;
    this.originCy = cy;
    this.originAngle = 0;
  }

  saveOrigin() {
    this.originCx = this.cx;
    this.originCy = this.cy;
    this.originAngle = this.angle;
  }

  resetPosition() {
    this.cx = this.originCx;
    this.cy = this.originCy;
    this.angle = this.originAngle;
  }

  hitTest(px, py) {
    // For pencil strokes, check distance to any point
    if (this.type === 'pencil' && this.points.length > 0) {
      const cos = Math.cos(this.angle);
      const sin = Math.sin(this.angle);
      // Transform click into object local space
      const lx = (px - this.cx) * cos + (py - this.cy) * sin;
      const ly = -(px - this.cx) * sin + (py - this.cy) * cos;
      for (let i = 0; i < this.points.length; i += 3) {
        const dx = lx - this.points[i].x;
        const dy = ly - this.points[i].y;
        if (dx * dx + dy * dy < 400) return true; // 20px threshold
      }
      return false;
    }
    const dx = px - this.cx;
    const dy = py - this.cy;
    return Math.sqrt(dx * dx + dy * dy) < this.boundingRadius + 10;
  }

  rotate(deltaRad) {
    this.angle += deltaRad;
  }

  render(ctx) {
    ctx.save();
    ctx.translate(this.cx, this.cy);
    ctx.rotate(this.angle);

    ctx.strokeStyle = this.selected ? '#ffaa44' : this.color;
    ctx.lineWidth = this.selected ? 4 : 3;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';

    if (this.type === 'pencil') {
      this.renderSmooth(ctx);
    } else if (this.type === 'stick') {
      this.renderStick(ctx);
    }

    ctx.restore();
  }

  renderSmooth(ctx) {
    const pts = this.points;
    if (pts.length < 2) return;

    ctx.beginPath();
    ctx.moveTo(pts[0].x, pts[0].y);

    if (pts.length === 2) {
      ctx.lineTo(pts[1].x, pts[1].y);
    } else {
      // Quadratic bezier smoothing through points
      for (let i = 1; i < pts.length - 1; i++) {
        const mx = (pts[i].x + pts[i + 1].x) / 2;
        const my = (pts[i].y + pts[i + 1].y) / 2;
        ctx.quadraticCurveTo(pts[i].x, pts[i].y, mx, my);
      }
      const last = pts[pts.length - 1];
      ctx.lineTo(last.x, last.y);
    }
    ctx.stroke();
  }

  renderStick(ctx) {
    const h = this.height || 80;
    const headR = h * 0.15;
    const bodyLen = h * 0.4;
    const legLen = h * 0.3;
    const armLen = h * 0.25;
    const armY = -h / 2 + headR * 2 + bodyLen * 0.3;

    ctx.beginPath();
    ctx.arc(0, -h / 2 + headR, headR, 0, Math.PI * 2);
    ctx.stroke();

    ctx.beginPath();
    ctx.moveTo(0, -h / 2 + headR * 2);
    ctx.lineTo(0, -h / 2 + headR * 2 + bodyLen);
    ctx.stroke();

    ctx.beginPath();
    ctx.moveTo(-armLen, armY);
    ctx.lineTo(armLen, armY);
    ctx.stroke();

    const hipY = -h / 2 + headR * 2 + bodyLen;
    ctx.beginPath();
    ctx.moveTo(0, hipY);
    ctx.lineTo(-legLen * 0.6, hipY + legLen);
    ctx.moveTo(0, hipY);
    ctx.lineTo(legLen * 0.6, hipY + legLen);
    ctx.stroke();

    this.boundingRadius = h / 2 + 5;
  }

  computeBounds() {
    if (this.type === 'stick') {
      this.boundingRadius = (this.height || 80) / 2 + 5;
      return;
    }
    let maxR = 0;
    for (const p of this.points) {
      const r = Math.sqrt(p.x * p.x + p.y * p.y);
      if (r > maxR) maxR = r;
    }
    this.boundingRadius = Math.max(maxR, 15);
  }
}

function createStickFigure(cx, cy) {
  const obj = new SimObject('stick', cx, cy);
  obj.height = 80;
  obj.boundingRadius = 45;
  obj.mass = 80;
  obj.dragCoefficient = 0.8;
  obj.crossSection = 0.7;
  obj.saveOrigin();
  return obj;
}
