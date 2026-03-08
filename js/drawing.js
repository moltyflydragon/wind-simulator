class DrawingManager {
  constructor(canvas, objectList) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.objects = objectList;
    this.currentTool = 'select';
    this.isDrawing = false;
    this.drawStart = null;
    this.currentObject = null;
    this.selectedObject = null;
    this.dragOffset = null;

    this.bindEvents();
  }

  bindEvents() {
    this.canvas.addEventListener('mousedown', e => this.onMouseDown(e));
    this.canvas.addEventListener('mousemove', e => this.onMouseMove(e));
    this.canvas.addEventListener('mouseup', e => this.onMouseUp(e));
    document.addEventListener('keydown', e => this.onKeyDown(e));
  }

  getPos(e) {
    const rect = this.canvas.getBoundingClientRect();
    return { x: e.clientX - rect.left, y: e.clientY - rect.top };
  }

  selectObject(obj) {
    if (this.selectedObject) this.selectedObject.selected = false;
    this.selectedObject = obj;
    if (obj) obj.selected = true;
  }

  onMouseDown(e) {
    const pos = this.getPos(e);
    this.isDrawing = true;
    this.drawStart = pos;

    if (this.currentTool === 'select') {
      // Try to select an object
      let hit = null;
      for (let i = this.objects.length - 1; i >= 0; i--) {
        if (this.objects[i].hitTest(pos.x, pos.y)) {
          hit = this.objects[i];
          break;
        }
      }
      this.selectObject(hit);
      if (hit) {
        this.dragOffset = { x: pos.x - hit.cx, y: pos.y - hit.cy };
      }
      return;
    }

    if (this.currentTool === 'stick') {
      const obj = createStickFigure(pos.x, pos.y);
      this.objects.push(obj);
      this.selectObject(obj);
      this.isDrawing = false;
      if (this.onObjectCreated) this.onObjectCreated(obj);
      return;
    }

    // Start drawing shape
    const obj = new SimObject(this.currentTool, pos.x, pos.y);

    if (this.currentTool === 'pencil') {
      obj.points.push({ x: 0, y: 0 });
    } else if (this.currentTool === 'line') {
      obj.points.push({ x: 0, y: 0 }, { x: 0, y: 0 });
    }

    this.currentObject = obj;
  }

  onMouseMove(e) {
    if (!this.isDrawing) return;
    const pos = this.getPos(e);

    if (this.currentTool === 'select' && this.selectedObject && this.dragOffset) {
      this.selectedObject.cx = pos.x - this.dragOffset.x;
      this.selectedObject.cy = pos.y - this.dragOffset.y;
      return;
    }

    if (!this.currentObject) return;

    const dx = pos.x - this.drawStart.x;
    const dy = pos.y - this.drawStart.y;

    switch (this.currentTool) {
      case 'pencil':
        this.currentObject.points.push({ x: dx, y: dy });
        break;
      case 'line':
        this.currentObject.points[1] = { x: dx, y: dy };
        break;
      case 'rect':
        this.currentObject.width = Math.abs(dx);
        this.currentObject.height = Math.abs(dy);
        this.currentObject.cx = this.drawStart.x + dx / 2;
        this.currentObject.cy = this.drawStart.y + dy / 2;
        break;
      case 'circle':
        this.currentObject.radius = Math.sqrt(dx * dx + dy * dy);
        break;
    }
  }

  onMouseUp(e) {
    if (!this.isDrawing) return;
    this.isDrawing = false;
    this.dragOffset = null;

    if (this.currentObject) {
      // Compute bounding radius
      this.finalizeBounds(this.currentObject);
      this.currentObject.saveOrigin();
      this.objects.push(this.currentObject);
      this.selectObject(this.currentObject);
      if (this.onObjectCreated) this.onObjectCreated(this.currentObject);
      this.currentObject = null;
    }
  }

  onKeyDown(e) {
    if (e.key === 'Delete' || e.key === 'Backspace') {
      if (this.selectedObject) {
        if (this.onObjectDeleted) this.onObjectDeleted(this.selectedObject);
        const idx = this.objects.indexOf(this.selectedObject);
        if (idx !== -1) this.objects.splice(idx, 1);
        this.selectObject(null);
      }
    }
  }

  finalizeBounds(obj) {
    switch (obj.type) {
      case 'pencil': {
        let maxR = 0;
        for (const p of obj.points) {
          const r = Math.sqrt(p.x * p.x + p.y * p.y);
          if (r > maxR) maxR = r;
        }
        obj.boundingRadius = Math.max(maxR, 10);
        break;
      }
      case 'line': {
        if (obj.points.length >= 2) {
          const p = obj.points[1];
          obj.boundingRadius = Math.max(Math.sqrt(p.x * p.x + p.y * p.y), 10);
        }
        break;
      }
      case 'rect':
        obj.boundingRadius = Math.sqrt(obj.width * obj.width + obj.height * obj.height) / 2;
        break;
      case 'circle':
        obj.boundingRadius = obj.radius;
        break;
    }
  }

  renderPreview() {
    if (!this.currentObject) return;
    this.currentObject.render(this.ctx);
  }
}
