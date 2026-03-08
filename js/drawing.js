class DrawingManager {
  constructor(canvas, objectList) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.objects = objectList;
    this.currentTool = 'draw';
    this.isDrawing = false;
    this.drawStart = null;
    this.currentObject = null;
    this.selected = []; // group selection
    this.dragOffset = null;
    this.undoStack = [];
    this.selectBox = null; // {x1,y1,x2,y2} for drag-select

    this.onObjectCreated = null;
    this.onObjectDeleted = null;

    this.canvas.addEventListener('mousedown', e => this.onMouseDown(e));
    this.canvas.addEventListener('mousemove', e => this.onMouseMove(e));
    this.canvas.addEventListener('mouseup', e => this.onMouseUp(e));
    document.addEventListener('keydown', e => this.onKeyDown(e));
  }

  getPos(e) {
    const rect = this.canvas.getBoundingClientRect();
    return { x: e.clientX - rect.left, y: e.clientY - rect.top };
  }

  get selectedObject() {
    return this.selected.length === 1 ? this.selected[0] : null;
  }

  selectNone() {
    for (const obj of this.selected) obj.selected = false;
    this.selected = [];
  }

  selectOne(obj) {
    this.selectNone();
    if (obj) {
      obj.selected = true;
      this.selected = [obj];
    }
  }

  selectGroup(objs) {
    this.selectNone();
    for (const obj of objs) {
      obj.selected = true;
    }
    this.selected = [...objs];
  }

  pushUndo() {
    // Snapshot current object list (shallow info for undo)
    this.undoStack.push(this.objects.length);
  }

  undo() {
    if (this.objects.length === 0) return;
    const obj = this.objects.pop();
    obj.selected = false;
    this.selected = this.selected.filter(s => s !== obj);
    if (this.onObjectDeleted) this.onObjectDeleted(obj);
  }

  onMouseDown(e) {
    const pos = this.getPos(e);
    this.isDrawing = true;
    this.drawStart = pos;

    if (this.currentTool === 'select') {
      // Check if clicking on an already-selected object (for group drag)
      for (const obj of this.selected) {
        if (obj.hitTest(pos.x, pos.y)) {
          // Start dragging entire selection
          this.dragOffset = { x: pos.x, y: pos.y };
          return;
        }
      }

      // Check if clicking a single unselected object
      let hit = null;
      for (let i = this.objects.length - 1; i >= 0; i--) {
        if (this.objects[i].hitTest(pos.x, pos.y)) {
          hit = this.objects[i];
          break;
        }
      }

      if (hit) {
        this.selectOne(hit);
        this.dragOffset = { x: pos.x, y: pos.y };
      } else {
        // Start box selection
        this.selectNone();
        this.selectBox = { x1: pos.x, y1: pos.y, x2: pos.x, y2: pos.y };
        this.dragOffset = null;
      }
      return;
    }

    if (this.currentTool === 'stick') {
      const obj = createStickFigure(pos.x, pos.y);
      this.objects.push(obj);
      this.selectOne(obj);
      this.isDrawing = false;
      if (this.onObjectCreated) this.onObjectCreated(obj);
      return;
    }

    // Draw tool: start a new pencil stroke
    const obj = new SimObject('pencil', pos.x, pos.y);
    obj.points.push({ x: 0, y: 0 });
    this.currentObject = obj;
  }

  onMouseMove(e) {
    if (!this.isDrawing) return;
    const pos = this.getPos(e);

    if (this.currentTool === 'select') {
      if (this.selectBox) {
        // Update drag-select box
        this.selectBox.x2 = pos.x;
        this.selectBox.y2 = pos.y;
      } else if (this.dragOffset && this.selected.length > 0) {
        // Drag selected objects
        const dx = pos.x - this.dragOffset.x;
        const dy = pos.y - this.dragOffset.y;
        for (const obj of this.selected) {
          obj.cx += dx;
          obj.cy += dy;
        }
        this.dragOffset = { x: pos.x, y: pos.y };
      }
      return;
    }

    // Drawing: add point (with distance threshold for smoothness)
    if (this.currentObject && this.currentTool === 'draw') {
      const dx = pos.x - this.drawStart.x;
      const dy = pos.y - this.drawStart.y;
      const pts = this.currentObject.points;
      const last = pts[pts.length - 1];
      const ddx = dx - last.x;
      const ddy = dy - last.y;
      // Only add points every ~4px for smooth curves without too many points
      if (ddx * ddx + ddy * ddy > 16) {
        pts.push({ x: dx, y: dy });
      }
    }
  }

  onMouseUp(e) {
    if (!this.isDrawing) return;
    this.isDrawing = false;

    if (this.currentTool === 'select' && this.selectBox) {
      // Finalize box selection
      const box = this.selectBox;
      const x1 = Math.min(box.x1, box.x2);
      const y1 = Math.min(box.y1, box.y2);
      const x2 = Math.max(box.x1, box.x2);
      const y2 = Math.max(box.y1, box.y2);

      // Only select if box is big enough (not just a click)
      if (x2 - x1 > 5 || y2 - y1 > 5) {
        const hits = this.objects.filter(obj =>
          obj.cx >= x1 && obj.cx <= x2 && obj.cy >= y1 && obj.cy <= y2
        );
        if (hits.length > 0) this.selectGroup(hits);
      }

      this.selectBox = null;
    }

    this.dragOffset = null;

    if (this.currentObject) {
      const obj = this.currentObject;
      this.currentObject = null;

      // Need at least a few points for a valid stroke
      if (obj.points.length < 3) return;

      obj.computeBounds();
      obj.saveOrigin();
      this.objects.push(obj);
      this.selectOne(obj);
      if (this.onObjectCreated) this.onObjectCreated(obj);
    }
  }

  onKeyDown(e) {
    if ((e.metaKey || e.ctrlKey) && e.key === 'z') {
      e.preventDefault();
      this.undo();
    }
    if (e.key === 'Delete' || e.key === 'Backspace') {
      this.deleteSelected();
    }
  }

  deleteSelected() {
    for (const obj of this.selected) {
      if (this.onObjectDeleted) this.onObjectDeleted(obj);
      const idx = this.objects.indexOf(obj);
      if (idx !== -1) this.objects.splice(idx, 1);
    }
    this.selected = [];
  }

  renderPreview(ctx) {
    // Draw current stroke in progress
    if (this.currentObject) {
      this.currentObject.render(ctx);
    }

    // Draw selection box
    if (this.selectBox) {
      const b = this.selectBox;
      ctx.strokeStyle = 'rgba(100,150,255,0.5)';
      ctx.lineWidth = 1;
      ctx.setLineDash([4, 4]);
      ctx.strokeRect(
        Math.min(b.x1, b.x2), Math.min(b.y1, b.y2),
        Math.abs(b.x2 - b.x1), Math.abs(b.y2 - b.y1)
      );
      ctx.setLineDash([]);
    }
  }
}
