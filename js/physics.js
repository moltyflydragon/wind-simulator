const GRAVITY = 9.81; // m/s^2
const SEA_LEVEL_AIR_DENSITY = 1.225; // kg/m^3
const SCALE_FACTOR = 10; // 1 meter = 10 pixels

// Realistic skydiver body positions
const BODY_CONFIGS = {
  bellyToEarth: { cd: 0.75, area: 0.7, label: 'Belly-to-Earth' },
  headDown:     { cd: 0.35, area: 0.3, label: 'Head Down' },
  sitFly:       { cd: 0.55, area: 0.5, label: 'Sit Fly' },
  tracking:     { cd: 0.45, area: 0.45, label: 'Tracking' },
  backFly:      { cd: 0.80, area: 0.75, label: 'Back Fly' },
  custom:       { cd: 0.7,  area: 0.7,  label: 'Custom' }
};

// Air density decreases with altitude (ISA model approximation)
function airDensityAtAltitude(altitudeMeters) {
  // Barometric formula: rho = rho0 * (1 - L*h/T0)^(gM/RL - 1)
  // Simplified exponential: rho = rho0 * exp(-h / H) where H ~ 8500m
  const scaleHeight = 8500;
  return SEA_LEVEL_AIR_DENSITY * Math.exp(-altitudeMeters / scaleHeight);
}

class PhysicsBody {
  constructor(obj) {
    this.obj = obj;
    this.velocity = new Vec2(0, 0);
    this.acceleration = new Vec2(0, 0);
    this.mass = obj.mass || 80;
    this.dragCoefficient = obj.dragCoefficient || 0.75;
    this.crossSection = obj.crossSection || 0.7;
    this.bodyConfig = 'bellyToEarth';

    // Altitude tracking (starts at 4000m, typical jump altitude)
    this.altitude = 4000;

    // Computed values
    this.terminalVelocity = 0;
    this.relativeWindSpeed = 0;
    this.relativeWindAngle = 0;
    this.liftForce = new Vec2(0, 0);
    this.dragForce = new Vec2(0, 0);
    this.airDensity = SEA_LEVEL_AIR_DENSITY;
    this.speedMps = 0; // actual speed in m/s
    this.speedMph = 0;
    this.speedKph = 0;
  }

  setBodyConfig(configName) {
    if (BODY_CONFIGS[configName]) {
      this.bodyConfig = configName;
      const cfg = BODY_CONFIGS[configName];
      this.dragCoefficient = cfg.cd;
      this.crossSection = cfg.area;
      this.obj.dragCoefficient = cfg.cd;
      this.obj.crossSection = cfg.area;
    }
  }

  computeAerodynamics(windVelocity) {
    // Air density at current altitude
    this.airDensity = airDensityAtAltitude(this.altitude);
    const rho = this.airDensity;

    // Relative wind = wind velocity - body velocity
    const relativeWind = windVelocity.sub(this.velocity);
    this.relativeWindSpeed = relativeWind.mag();

    // Angle of attack: angle between body orientation and relative wind
    const bodyDir = Vec2.fromAngle(this.obj.angle);
    if (this.relativeWindSpeed > 0.01) {
      const windDir = relativeWind.normalize();
      const dot = Math.max(-1, Math.min(1, bodyDir.dot(windDir)));
      this.relativeWindAngle = Math.acos(dot);
    } else {
      this.relativeWindAngle = 0;
    }

    // Effective drag coefficient varies with angle of attack
    // At 0 AoA, body is streamlined; at 90 AoA, max cross-section
    const aoaNorm = this.relativeWindAngle / (Math.PI / 2); // 0..1 over 0..90deg
    const cdMultiplier = 0.6 + 0.4 * Math.pow(Math.abs(Math.sin(this.relativeWindAngle)), 1.2);
    const effectiveCd = this.dragCoefficient * cdMultiplier;

    // Cross-section also varies: belly presents more area than profile
    const effectiveArea = this.crossSection * (0.5 + 0.5 * Math.abs(Math.sin(this.relativeWindAngle)));

    // Drag force: F_drag = 0.5 * rho * v^2 * Cd * A (direction: along relative wind)
    const v2 = this.relativeWindSpeed * this.relativeWindSpeed;
    const dragMag = 0.5 * rho * v2 * effectiveCd * effectiveArea;

    if (this.relativeWindSpeed > 0.01) {
      this.dragForce = relativeWind.normalize().scale(dragMag);
    } else {
      this.dragForce = new Vec2(0, 0);
    }

    // Lift force: perpendicular to relative wind, proportional to sin(2*AoA)
    // Realistic thin-body lift: Cl ~ 2*pi*sin(AoA)*cos(AoA) simplified
    const liftCoeff = 0.4 * Math.sin(2 * this.relativeWindAngle);
    const liftMag = 0.5 * rho * v2 * Math.abs(liftCoeff) * effectiveArea;

    if (this.relativeWindSpeed > 0.01 && Math.abs(liftCoeff) > 0.001) {
      const windNorm = relativeWind.normalize();
      // Lift perpendicular to wind, sign based on body orientation relative to wind
      const cross = bodyDir.x * windNorm.y - bodyDir.y * windNorm.x;
      const liftSign = cross >= 0 ? 1 : -1;
      const liftDir = new Vec2(-windNorm.y, windNorm.x);
      this.liftForce = liftDir.scale(liftMag * liftSign);
    } else {
      this.liftForce = new Vec2(0, 0);
    }

    // Terminal velocity at current altitude/config
    this.terminalVelocity = Math.sqrt(
      (2 * this.mass * GRAVITY) / (rho * effectiveCd * effectiveArea)
    );
  }

  update(dt, windVelocity) {
    // RK2 (midpoint method) for better accuracy
    // k1
    this.computeAerodynamics(windVelocity);
    const gravity = new Vec2(0, this.mass * GRAVITY);
    const force1 = gravity.add(this.dragForce).add(this.liftForce);
    const acc1 = force1.scale(1 / this.mass);

    // Estimate midpoint state
    const midVel = this.velocity.add(acc1.scale(dt * 0.5));
    const savedVel = this.velocity;
    this.velocity = midVel;

    // k2 at midpoint
    this.computeAerodynamics(windVelocity);
    const force2 = gravity.add(this.dragForce).add(this.liftForce);
    const acc2 = force2.scale(1 / this.mass);

    // Restore and apply RK2 step
    this.velocity = savedVel;
    this.acceleration = acc2;
    this.velocity = this.velocity.add(acc2.scale(dt));

    // Speed conversions
    this.speedMps = this.velocity.mag();
    this.speedMph = this.speedMps * 2.237;
    this.speedKph = this.speedMps * 3.6;

    // Update altitude (positive vy = falling = losing altitude)
    this.altitude -= this.velocity.y * dt;
    if (this.altitude < 0) this.altitude = 0;

    // Pixel displacement
    const displacement = this.velocity.scale(dt * SCALE_FACTOR);
    this.obj.cx += displacement.x;
    this.obj.cy += displacement.y;
  }

  reset() {
    this.velocity = new Vec2(0, 0);
    this.acceleration = new Vec2(0, 0);
    this.altitude = 4000;
  }
}

class PhysicsEngine {
  constructor() {
    this.bodies = [];
    this.timeScale = 1.0; // slow-mo or fast-forward
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
    const scaledDt = dt * this.timeScale;
    // Sub-step for stability at high speeds
    const steps = Math.max(1, Math.ceil(scaledDt / 0.005));
    const subDt = scaledDt / steps;

    for (let s = 0; s < steps; s++) {
      for (const body of this.bodies) {
        if (body.obj.pinned) continue;
        body.update(subDt, windVelocity);
      }
    }
  }

  reset() {
    for (const body of this.bodies) {
      body.reset();
    }
  }
}
