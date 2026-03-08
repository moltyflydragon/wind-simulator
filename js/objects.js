let objectIdCounter = 0;

class SimObject {
  constructor(type, cx, cy) {
    this.id = ++objectIdCounter;
    this.type = type;
    this.cx = cx;
    this.cy = cy;
    this.angle = 0; // radians
    this.points = []; // for freeform/line shapes
    this.width = 0;
    this.height = 0;
    this.radius = 0;
    this.boundingRadius = 30;
    this.pinned = false;
    this.mass = 80;
    this.dragCoefficient = 0.7;
    this.crossSection = 0.7;
    this.color = '#e0e0f0';
    this.selected = false;

    // Store original position for reset
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
    const dx = px - this.cx;
    const dy = py - this.cy;
    return Math.sqrt(dx * dx + dy * dy) < this.boundingRadius + 10;
  }

  rotate(deltaRad) {
    this.angle += deltaRad;
  }

  flipHorizontal() {
    // Mirror points around center
    for (const pt of this.points) {
      pt.x = -pt.x;
    }
    this.angle = -this.angle;
  }

  render(ctx) {
    ctx.save();
    ctx.translate(this.cx, this.cy);
    ctx.rotate(this.angle);

    ctx.strokeStyle = this.selected ? '#ffaa44' : this.color;
    ctx.lineWidth = this.selected ? 2.5 : 1.5;
    ctx.fillStyle = 'transparent';

    switch (this.type) {
      case 'pencil':
        this.renderPencil(ctx);
        break;
      case 'line':
        this.renderLine(ctx);
        break;
      case 'rect':
        this.renderRect(ctx);
        break;
      case 'circle':
        this.renderCircle(ctx);
        break;
      case 'stick':
        this.renderStick(ctx);
        break;
    }


    ctx.restore();
  }

  renderPencil(ctx) {
    if (this.points.length < 2) return;
    ctx.beginPath();
    ctx.moveTo(this.points[0].x, this.points[0].y);
    for (let i = 1; i < this.points.length; i++) {
      ctx.lineTo(this.points[i].x, this.points[i].y);
    }
    ctx.stroke();
  }

  renderLine(ctx) {
    if (this.points.length < 2) return;
    ctx.beginPath();
    ctx.moveTo(this.points[0].x, this.points[0].y);
    ctx.lineTo(this.points[1].x, this.points[1].y);
    ctx.stroke();
  }

  renderRect(ctx) {
    ctx.strokeRect(-this.width / 2, -this.height / 2, this.width, this.height);
  }

  renderCircle(ctx) {
    ctx.beginPath();
    ctx.arc(0, 0, this.radius, 0, Math.PI * 2);
    ctx.stroke();
  }

  renderStick(ctx) {
    const h = this.height || 60;
    const headR = h * 0.15;
    const bodyLen = h * 0.4;
    const legLen = h * 0.3;
    const armLen = h * 0.25;
    const armY = -h / 2 + headR * 2 + bodyLen * 0.3;

    // Head
    ctx.beginPath();
    ctx.arc(0, -h / 2 + headR, headR, 0, Math.PI * 2);
    ctx.stroke();

    // Body
    ctx.beginPath();
    ctx.moveTo(0, -h / 2 + headR * 2);
    ctx.lineTo(0, -h / 2 + headR * 2 + bodyLen);
    ctx.stroke();

    // Arms
    ctx.beginPath();
    ctx.moveTo(-armLen, armY);
    ctx.lineTo(armLen, armY);
    ctx.stroke();

    // Legs
    const hipY = -h / 2 + headR * 2 + bodyLen;
    ctx.beginPath();
    ctx.moveTo(0, hipY);
    ctx.lineTo(-legLen * 0.6, hipY + legLen);
    ctx.moveTo(0, hipY);
    ctx.lineTo(legLen * 0.6, hipY + legLen);
    ctx.stroke();

    this.boundingRadius = h / 2 + 5;
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
