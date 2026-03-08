const GRAVITY = 9.81;
const AIR_DENSITY = 1.225; // kg/m^3 at sea level

class PhysicsBody {
  constructor(obj) {
    this.obj = obj;
    this.velocity = new Vec2(0, 0);
    this.acceleration = new Vec2(0, 0);
    this.mass = obj.mass || 80; // kg (skydiver default)
    this.dragCoefficient = obj.dragCoefficient || 0.7;
    this.crossSection = obj.crossSection || 0.7; // m^2
    this.terminalVelocity = 0;
    this.relativeWindSpeed = 0;
    this.relativeWindAngle = 0;
    this.liftForce = new Vec2(0, 0);
    this.dragForce = new Vec2(0, 0);
  }

  computeAerodynamics(windVelocity) {
    // Relative wind = wind velocity - body velocity
    const relativeWind = windVelocity.sub(this.velocity);
    this.relativeWindSpeed = relativeWind.mag();

    // Angle of attack: angle between body orientation and relative wind
    const bodyDir = Vec2.fromAngle(this.obj.angle);
    if (this.relativeWindSpeed > 0.01) {
      const windDir = relativeWind.normalize();
      const dot = bodyDir.dot(windDir);
      this.relativeWindAngle = Math.acos(Math.max(-1, Math.min(1, dot)));
    } else {
      this.relativeWindAngle = 0;
    }

    // Drag force: F = 0.5 * rho * v^2 * Cd * A
    // Cd varies with angle of attack
    const effectiveCd = this.dragCoefficient * (0.5 + 0.5 * Math.abs(Math.sin(this.relativeWindAngle)));
    const dragMag = 0.5 * AIR_DENSITY * this.relativeWindSpeed * this.relativeWindSpeed * effectiveCd * this.crossSection;

    if (this.relativeWindSpeed > 0.01) {
      this.dragForce = relativeWind.normalize().scale(dragMag);
    } else {
      this.dragForce = new Vec2(0, 0);
    }

    // Simple lift (perpendicular to relative wind, proportional to angle of attack)
    const liftCoeff = 0.3 * Math.sin(2 * this.relativeWindAngle);
    const liftMag = 0.5 * AIR_DENSITY * this.relativeWindSpeed * this.relativeWindSpeed * liftCoeff * this.crossSection;
    if (this.relativeWindSpeed > 0.01) {
      const windNorm = relativeWind.normalize();
      const liftDir = new Vec2(-windNorm.y, windNorm.x);
      this.liftForce = liftDir.scale(liftMag);
    } else {
      this.liftForce = new Vec2(0, 0);
    }

    // Terminal velocity for reference
    this.terminalVelocity = Math.sqrt((2 * this.mass * GRAVITY) / (AIR_DENSITY * effectiveCd * this.crossSection));
  }

  update(dt, windVelocity) {
    this.computeAerodynamics(windVelocity);

    // Gravity (downward = positive Y in screen coords)
    const gravity = new Vec2(0, this.mass * GRAVITY);

    // Total force
    const totalForce = gravity.add(this.dragForce).add(this.liftForce);

    // F = ma -> a = F/m
    this.acceleration = totalForce.scale(1 / this.mass);

    // Semi-implicit Euler integration
    this.velocity = this.velocity.add(this.acceleration.scale(dt));

    // Scale movement to pixels (1 meter = 10 pixels for visualization)
    const pixelScale = 10;
    const displacement = this.velocity.scale(dt * pixelScale);

    // Move the object's center
    this.obj.cx += displacement.x;
    this.obj.cy += displacement.y;
  }
}

class PhysicsEngine {
  constructor() {
    this.bodies = [];
  }

  addBody(obj) {
    const body = new PhysicsBody(obj);
    this.bodies.push(body);
    return body;
  }

  removeBody(obj) {
    this.bodies = this.bodies.filter(b => b.obj !== obj);
  }

  getBody(obj) {
    return this.bodies.find(b => b.obj === obj);
  }

  update(dt, windVelocity) {
    for (const body of this.bodies) {
      if (body.obj.pinned) continue;
      body.update(dt, windVelocity);
    }
  }

  reset() {
    for (const body of this.bodies) {
      body.velocity = new Vec2(0, 0);
      body.acceleration = new Vec2(0, 0);
    }
  }
}
