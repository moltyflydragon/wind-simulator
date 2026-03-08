#include <SDL.h>
#include <OpenGL/gl3.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <deque>

// ── Vec2 ────────────────────────────────────────────────────────────────────

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}
    Vec2 operator+(Vec2 v) const { return {x+v.x, y+v.y}; }
    Vec2 operator-(Vec2 v) const { return {x-v.x, y-v.y}; }
    Vec2 operator*(float s) const { return {x*s, y*s}; }
    Vec2& operator+=(Vec2 v) { x+=v.x; y+=v.y; return *this; }
    float dot(Vec2 v) const { return x*v.x + y*v.y; }
    float mag2() const { return x*x + y*y; }
    float mag() const { return sqrtf(mag2()); }
    Vec2 norm() const { float m=mag(); return m>1e-6f ? Vec2{x/m,y/m} : Vec2{0,0}; }
    Vec2 perp() const { return {-y, x}; }
    Vec2 rotate(float a) const {
        float c=cosf(a), s=sinf(a);
        return {x*c - y*s, x*s + y*c};
    }
};

// ── Stroke ──────────────────────────────────────────────────────────────────

struct Stroke {
    std::vector<Vec2> points; // in local space (relative to center)
    Vec2 center;
    float radius = 0;

    void computeBounds() {
        if (points.empty()) return;
        // Compute centroid
        Vec2 sum{0,0};
        for (auto& p : points) sum += p;
        Vec2 centroid = sum * (1.0f / points.size());
        // Re-center points
        for (auto& p : points) p = p - centroid;
        center = center + centroid;
        // Bounding radius
        radius = 0;
        for (auto& p : points) {
            float r = p.mag();
            if (r > radius) radius = r;
        }
        radius = fmaxf(radius, 10.0f);
    }
};

// ── Body (grouped strokes = single physics object) ─────────────────────────

struct Body {
    std::vector<Stroke> strokes;
    Vec2 center;
    float angle = 0;
    float radius = 30; // effective radius for flow computation

    // Physics
    float mass = 80.0f;      // kg
    float dragCoef = 0.75f;
    float crossSection = 0.7f; // m^2
    float altitude = 4000.0f;  // m
    Vec2 velocity{0,0};
    Vec2 acceleration{0,0};
    float speedMps = 0;
    float terminalV = 0;
    float relWindSpeed = 0;
    float relWindAngle = 0; // AoA in degrees
    float airDensity = 1.225f;

    // State
    bool selected = false;
    bool pinned = false;
    Vec2 originCenter;
    float originAngle = 0;

    void saveOrigin() { originCenter = center; originAngle = angle; }
    void resetToOrigin() {
        center = originCenter; angle = originAngle;
        velocity = {0,0}; acceleration = {0,0};
        altitude = 4000.0f; speedMps = 0;
    }

    void computeRadius() {
        radius = 0;
        for (auto& s : strokes) {
            Vec2 off = s.center - center;
            float r = off.mag() + s.radius;
            if (r > radius) radius = r;
        }
        radius = fmaxf(radius, 15.0f);
    }

    void recomputeCenter() {
        if (strokes.empty()) return;
        Vec2 sum{0,0};
        for (auto& s : strokes) sum += s.center;
        Vec2 newCenter = sum * (1.0f / strokes.size());
        // Offset stroke centers relative to new body center
        for (auto& s : strokes) s.center = s.center - newCenter;
        center = newCenter;
        computeRadius();
    }

    bool hitTest(Vec2 p) const {
        // Test against each stroke's points (transformed)
        float c = cosf(angle), s = sinf(angle);
        for (auto& st : strokes) {
            for (size_t i = 0; i < st.points.size(); i += 2) {
                // Rotate stroke center by body angle, then add body center
                Vec2 sc = {st.center.x*c - st.center.y*s, st.center.x*s + st.center.y*c};
                Vec2 pt = {st.points[i].x*c - st.points[i].y*s, st.points[i].x*s + st.points[i].y*c};
                Vec2 worldPt = center + sc + pt;
                float dx = p.x - worldPt.x;
                float dy = p.y - worldPt.y;
                if (dx*dx + dy*dy < 400.0f) return true;
            }
        }
        return false;
    }
};

// ── Potential Flow Field ────────────────────────────────────────────────────
// Real physics: flow around cylinders using potential flow theory.
// Each body is approximated as a cylinder. The velocity field is computed
// from the analytical solution (dipole perturbation), NOT fake push-away.

Vec2 flowFieldAt(Vec2 pos, Vec2 wind, const std::vector<Body>& bodies) {
    // Start with uniform wind
    Vec2 vel = wind;

    for (auto& body : bodies) {
        Vec2 d = pos - body.center;
        float r2 = d.mag2();
        float R = body.radius;
        float R2 = R * R;

        // Skip if inside or too close to body
        if (r2 < R2 * 1.05f) {
            // Inside body: kill velocity (no flow inside solid)
            return {0, 0};
        }

        // Potential flow dipole perturbation:
        // For uniform flow U past cylinder radius R:
        // u_x = U_x - R^2/r^4 * (U_x*(dx^2 - dy^2) + 2*U_y*dx*dy)
        // u_y = U_y - R^2/r^4 * (U_y*(dy^2 - dx^2) + 2*U_x*dx*dy)
        float r4 = r2 * r2;
        float factor = R2 / r4;
        float dxdy2 = 2.0f * d.x * d.y;
        float dx2mdy2 = d.x * d.x - d.y * d.y;

        vel.x -= factor * (wind.x * dx2mdy2 + wind.y * dxdy2);
        vel.y -= factor * (wind.y * (-dx2mdy2) + wind.x * dxdy2);
    }

    return vel;
}

// ── Particle System (SoA, cache-friendly) ───────────────────────────────────

static constexpr int MAX_PARTICLES = 100000;

struct Particles {
    float* x;
    float* y;
    float* life;
    int count = 0;
    float spawnAccum = 0;

    GLuint vao = 0, vbo = 0;

    void init() {
        x = new float[MAX_PARTICLES];
        y = new float[MAX_PARTICLES];
        life = new float[MAX_PARTICLES];

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        // Will hold interleaved x,y,life
        glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), 0);
        glBindVertexArray(0);
    }

    void update(float dt, Vec2 wind, Vec2 windDir, float windSpeed, int screenW, int screenH,
                const std::vector<Body>& bodies) {
        if (windSpeed < 0.5f) { count = 0; return; }

        // Spawn: rate proportional to dt and wind speed
        spawnAccum += windSpeed * 800.0f * dt;
        int toSpawn = (int)spawnAccum;
        spawnAccum -= toSpawn;

        float w = (float)screenW, h = (float)screenH;
        float absDx = fabsf(windDir.x), absDy = fabsf(windDir.y);
        float spd = windSpeed * 10.0f;

        for (int i = 0; i < toSpawn && count < MAX_PARTICLES; i++) {
            int idx = count++;
            float r1 = (float)rand() / RAND_MAX;
            float r2 = (float)rand() / RAND_MAX;

            if (r1 < 0.1f) {
                // Scatter spawn across canvas
                x[idx] = r2 * w;
                y[idx] = ((float)rand()/RAND_MAX) * h;
            } else if (absDx > absDy) {
                x[idx] = windDir.x > 0 ? -r2*15.0f : w + r2*15.0f;
                y[idx] = r2 * h;
            } else {
                x[idx] = r2 * w;
                y[idx] = windDir.y > 0 ? -r2*15.0f : h + r2*15.0f;
            }
            life[idx] = 0.5f + ((float)rand()/RAND_MAX) * 0.5f;
        }

        float decay = 0.3f * dt;
        int alive = 0;

        for (int i = 0; i < count; i++) {
            float plife = life[i] - decay;
            if (plife <= 0 || x[i] < -50 || x[i] > w+50 || y[i] < -50 || y[i] > h+50) continue;

            // Compute flow field at particle position (real potential flow)
            Vec2 flow = flowFieldAt({x[i], y[i]}, wind, bodies);

            // Move particle along flow field
            x[alive] = x[i] + flow.x * dt;
            y[alive] = y[i] + flow.y * dt;
            life[alive] = plife;
            alive++;
        }
        count = alive;
    }

    void uploadAndRender(int screenW, int screenH) {
        if (count == 0) return;

        // Upload interleaved data
        std::vector<float> data(count * 3);
        for (int i = 0; i < count; i++) {
            data[i*3+0] = x[i];
            data[i*3+1] = y[i];
            data[i*3+2] = life[i];
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * 3 * sizeof(float), data.data());

        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, count);
        glBindVertexArray(0);
    }

    void cleanup() {
        delete[] x; delete[] y; delete[] life;
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
};

// ── Shaders ─────────────────────────────────────────────────────────────────

static const char* particleVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aData; // x, y, life
uniform vec2 uScreen;
out float vLife;
void main() {
    vec2 ndc = vec2(aData.x / uScreen.x * 2.0 - 1.0, 1.0 - aData.y / uScreen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    gl_PointSize = 2.0;
    vLife = aData.z;
}
)";

static const char* particleFragSrc = R"(
#version 330 core
in float vLife;
out vec4 fragColor;
void main() {
    float alpha = vLife * 0.4;
    fragColor = vec4(0.55, 0.7, 1.0, alpha);
}
)";

static const char* lineVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
uniform vec2 uScreen;
uniform vec4 uColor;
out vec4 vColor;
void main() {
    vec2 ndc = vec2(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = uColor;
}
)";

static const char* lineFragSrc = R"(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[512]; glGetShaderInfoLog(s,512,nullptr,buf); printf("Shader error: %s\n",buf); }
    return s;
}

GLuint createProgram(const char* vertSrc, const char* fragSrc) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ── Stroke Rendering ────────────────────────────────────────────────────────
// Renders thick lines as triangle strips for proper width

struct LineRenderer {
    GLuint vao = 0, vbo = 0;
    GLuint program = 0;

    void init() {
        program = createProgram(lineVertSrc, lineFragSrc);
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glBindVertexArray(0);
    }

    // Generate triangle strip vertices for a thick polyline
    void drawThickLine(const std::vector<Vec2>& pts, Vec2 offset, float angle,
                       float thickness, float r, float g, float b, float a,
                       int screenW, int screenH) {
        if (pts.size() < 2) return;

        // Transform points: rotate by angle then translate by offset
        std::vector<Vec2> world(pts.size());
        float ca = cosf(angle), sa = sinf(angle);
        for (size_t i = 0; i < pts.size(); i++) {
            Vec2 p = pts[i];
            world[i] = {offset.x + p.x*ca - p.y*sa, offset.y + p.x*sa + p.y*ca};
        }

        // Build triangle strip for thick line
        std::vector<float> verts;
        float half = thickness * 0.5f;
        for (size_t i = 0; i < world.size(); i++) {
            Vec2 dir;
            if (i == 0) dir = (world[1] - world[0]).norm();
            else if (i == world.size()-1) dir = (world[i] - world[i-1]).norm();
            else dir = (world[i+1] - world[i-1]).norm().norm();

            Vec2 n = dir.perp();
            verts.push_back(world[i].x + n.x*half);
            verts.push_back(world[i].y + n.y*half);
            verts.push_back(world[i].x - n.x*half);
            verts.push_back(world[i].y - n.y*half);
        }

        glUseProgram(program);
        glUniform2f(glGetUniformLocation(program, "uScreen"), (float)screenW, (float)screenH);
        glUniform4f(glGetUniformLocation(program, "uColor"), r, g, b, a);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, (int)(verts.size()/2));
        glBindVertexArray(0);
    }

    void cleanup() {
        glDeleteProgram(program);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
};

// ── Physics ─────────────────────────────────────────────────────────────────

static constexpr float GRAVITY = 9.81f;
static constexpr float SEA_LEVEL_RHO = 1.225f;
static constexpr float PX_PER_METER = 10.0f;

float airDensity(float alt) {
    return SEA_LEVEL_RHO * expf(-alt / 8500.0f);
}

void physicsStep(Body& body, float dt, Vec2 windVelocity) {
    if (body.pinned) return;

    float rho = airDensity(body.altitude);
    body.airDensity = rho;

    // Relative wind
    Vec2 relWind = windVelocity - body.velocity;
    body.relWindSpeed = relWind.mag();

    // Angle of attack
    Vec2 bodyDir = Vec2{1,0}.rotate(body.angle);
    if (body.relWindSpeed > 0.01f) {
        float d = bodyDir.dot(relWind.norm());
        d = fmaxf(-1.0f, fminf(1.0f, d));
        body.relWindAngle = acosf(d) * 180.0f / M_PI;
    } else {
        body.relWindAngle = 0;
    }

    float aoaRad = body.relWindAngle * M_PI / 180.0f;

    // Effective Cd varies with AoA
    float cdMul = 0.6f + 0.4f * powf(fabsf(sinf(aoaRad)), 1.2f);
    float effCd = body.dragCoef * cdMul;
    float effArea = body.crossSection * (0.5f + 0.5f * fabsf(sinf(aoaRad)));

    // Drag: F = 0.5 * rho * v^2 * Cd * A
    float v2 = body.relWindSpeed * body.relWindSpeed;
    float dragMag = 0.5f * rho * v2 * effCd * effArea;
    Vec2 dragForce = body.relWindSpeed > 0.01f ? relWind.norm() * dragMag : Vec2{0,0};

    // Lift: perpendicular to wind
    float liftCoef = 0.4f * sinf(2.0f * aoaRad);
    float liftMag = 0.5f * rho * v2 * fabsf(liftCoef) * effArea;
    Vec2 liftForce{0,0};
    if (body.relWindSpeed > 0.01f && fabsf(liftCoef) > 0.001f) {
        Vec2 wn = relWind.norm();
        float cross = bodyDir.x * wn.y - bodyDir.y * wn.x;
        Vec2 liftDir = wn.perp();
        liftForce = liftDir * (liftMag * (cross >= 0 ? 1.0f : -1.0f));
    }

    // Gravity
    Vec2 gravity{0, body.mass * GRAVITY};
    Vec2 total = gravity + dragForce + liftForce;
    body.acceleration = total * (1.0f / body.mass);
    body.velocity += body.acceleration * dt;
    body.speedMps = body.velocity.mag();
    body.altitude -= body.velocity.y * dt;
    if (body.altitude < 0) body.altitude = 0;
    body.terminalV = sqrtf(2.0f * body.mass * GRAVITY / (rho * effCd * effArea));

    body.center += body.velocity * (dt * PX_PER_METER);
}

// ── Application ─────────────────────────────────────────────────────────────

struct App {
    // Window
    SDL_Window* window = nullptr;
    SDL_GLContext gl;
    int screenW = 1400, screenH = 900;

    // Rendering
    GLuint particleProgram = 0;
    Particles particles;
    LineRenderer lineRenderer;

    // State
    std::vector<Body> bodies;
    std::deque<Body> undoStack; // snapshot of removed bodies for undo

    // Drawing
    bool isDrawing = false;
    Stroke currentStroke;
    Vec2 drawOrigin;

    // Selection
    std::vector<int> selected; // indices into bodies
    bool isDragging = false;
    Vec2 dragStart;
    bool isBoxSelecting = false;
    Vec2 boxStart, boxEnd;

    // Tool: 0=draw, 1=select, 2=stick
    int tool = 0;

    // Simulation
    bool simRunning = false;
    float windSpeed = 30.0f;
    float windAngleDeg = 270.0f;

    Vec2 windDir() const {
        float a = windAngleDeg * M_PI / 180.0f;
        return {cosf(a), sinf(a)};
    }
    Vec2 windVelocity() const { return windDir() * windSpeed; }
    Vec2 windPixelVel() const { return windDir() * (windSpeed * 10.0f); }

    void init() {
        SDL_Init(SDL_INIT_VIDEO);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

        window = SDL_CreateWindow("Wind Simulator",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            screenW, screenH, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        gl = SDL_GL_CreateContext(window);
        SDL_GL_SetSwapInterval(1); // vsync

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_MULTISAMPLE);
        glEnable(GL_LINE_SMOOTH);

        // ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        // Darken the style
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4;
        style.FrameRounding = 2;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.09f, 0.95f);

        ImGui_ImplSDL2_InitForOpenGL(window, gl);
        ImGui_ImplOpenGL3_Init("#version 330");

        // Shaders
        particleProgram = createProgram(particleVertSrc, particleFragSrc);
        particles.init();
        lineRenderer.init();
    }

    void deselectAll() {
        for (int i : selected) bodies[i].selected = false;
        selected.clear();
    }

    void selectBody(int idx) {
        bodies[idx].selected = true;
        if (std::find(selected.begin(), selected.end(), idx) == selected.end())
            selected.push_back(idx);
    }

    void groupSelected() {
        // Merge all selected bodies into one
        if (selected.size() < 2) return;

        Body merged;
        for (int idx : selected) {
            Body& b = bodies[idx];
            float ca = cosf(b.angle), sa = sinf(b.angle);
            for (auto& s : b.strokes) {
                Stroke ns = s;
                // Transform stroke center by body angle
                ns.center = {
                    b.center.x + s.center.x * ca - s.center.y * sa,
                    b.center.y + s.center.x * sa + s.center.y * ca
                };
                // Rotate stroke points by body angle
                for (auto& p : ns.points) p = p.rotate(b.angle);
                merged.strokes.push_back(ns);
            }
            merged.mass += b.mass;
            merged.crossSection += b.crossSection;
        }

        // Remove selected bodies (in reverse order to preserve indices)
        std::sort(selected.begin(), selected.end(), std::greater<int>());
        for (int idx : selected) bodies.erase(bodies.begin() + idx);
        selected.clear();

        merged.recomputeCenter();
        merged.dragCoef = 0.75f;
        merged.saveOrigin();
        merged.selected = true;
        bodies.push_back(merged);
        selected.push_back((int)bodies.size() - 1);
    }

    void undo() {
        if (bodies.empty()) return;
        // Remove last body
        Body b = bodies.back();
        bodies.pop_back();
        undoStack.push_back(b);
        // Clean up selection
        selected.erase(std::remove_if(selected.begin(), selected.end(),
            [&](int i){ return i >= (int)bodies.size(); }), selected.end());
    }

    void handleEvent(SDL_Event& e) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse || io.WantCaptureKeyboard) return;

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            int mx = e.button.x, my = e.button.y;
            // Scale for high-DPI
            int dw, dh; SDL_GL_GetDrawableSize(window, &dw, &dh);
            int ww, wh; SDL_GetWindowSize(window, &ww, &wh);
            float sx = (float)dw/ww, sy = (float)dh/wh;
            float fx = mx * sx, fy = my * sy;

            if (tool == 0) { // Draw
                isDrawing = true;
                drawOrigin = {fx, fy};
                currentStroke = Stroke{};
                currentStroke.center = drawOrigin;
                currentStroke.points.push_back({0, 0});
            } else if (tool == 1) { // Select
                // Check if clicking on already-selected body for drag
                for (int idx : selected) {
                    if (bodies[idx].hitTest({fx, fy})) {
                        isDragging = true;
                        dragStart = {fx, fy};
                        return;
                    }
                }
                // Check if clicking any body
                deselectAll();
                for (int i = (int)bodies.size()-1; i >= 0; i--) {
                    if (bodies[i].hitTest({fx, fy})) {
                        selectBody(i);
                        isDragging = true;
                        dragStart = {fx, fy};
                        return;
                    }
                }
                // Start box select
                isBoxSelecting = true;
                boxStart = boxEnd = {fx, fy};
            } else if (tool == 2) { // Stick figure
                Body b;
                Stroke head, bodyLine, arms, legL, legR;
                float h = 80.0f, headR = h*0.15f;
                // Head (circle approximation)
                for (int i = 0; i <= 20; i++) {
                    float a = (float)i / 20.0f * 2.0f * M_PI;
                    head.points.push_back({cosf(a)*headR, -h/2+headR + sinf(a)*headR});
                }
                head.center = {0, 0};
                head.computeBounds();
                // Body
                bodyLine.points.push_back({0, -h/2 + headR*2});
                bodyLine.points.push_back({0, -h/2 + headR*2 + h*0.4f});
                bodyLine.center = {0, 0};
                bodyLine.computeBounds();
                // Arms
                float armY = -h/2 + headR*2 + h*0.12f;
                arms.points.push_back({-h*0.25f, armY});
                arms.points.push_back({h*0.25f, armY});
                arms.center = {0, 0};
                arms.computeBounds();
                // Legs
                float hipY = -h/2 + headR*2 + h*0.4f;
                legL.points.push_back({0, hipY});
                legL.points.push_back({-h*0.18f, hipY + h*0.3f});
                legL.center = {0, 0};
                legL.computeBounds();
                legR.points.push_back({0, hipY});
                legR.points.push_back({h*0.18f, hipY + h*0.3f});
                legR.center = {0, 0};
                legR.computeBounds();

                b.strokes = {head, bodyLine, arms, legL, legR};
                b.center = {fx, fy};
                b.recomputeCenter();
                b.center = {fx, fy}; // force to click pos
                b.mass = 80.0f;
                b.dragCoef = 0.8f;
                b.crossSection = 0.7f;
                b.saveOrigin();
                bodies.push_back(b);
            }
        }

        if (e.type == SDL_MOUSEMOTION) {
            int dw, dh; SDL_GL_GetDrawableSize(window, &dw, &dh);
            int ww, wh; SDL_GetWindowSize(window, &ww, &wh);
            float sx = (float)dw/ww, sy = (float)dh/wh;
            float fx = e.motion.x * sx, fy = e.motion.y * sy;

            if (isDrawing) {
                Vec2 rel = {fx - drawOrigin.x, fy - drawOrigin.y};
                auto& pts = currentStroke.points;
                Vec2 last = pts.back();
                float dd = (rel - last).mag2();
                if (dd > 16.0f) { // ~4px threshold for smoothness
                    pts.push_back(rel);
                }
            }
            if (isDragging && !selected.empty()) {
                Vec2 cur = {fx, fy};
                Vec2 delta = cur - dragStart;
                for (int idx : selected) bodies[idx].center += delta;
                dragStart = cur;
            }
            if (isBoxSelecting) {
                boxEnd = {fx, fy};
            }
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (isDrawing && currentStroke.points.size() >= 3) {
                currentStroke.computeBounds();
                Body b;
                b.strokes.push_back(currentStroke);
                b.center = currentStroke.center;
                b.radius = currentStroke.radius;
                b.mass = 5.0f; // light drawn object
                b.crossSection = b.radius / 100.0f;
                b.dragCoef = 0.5f;
                b.saveOrigin();
                bodies.push_back(b);
            }
            isDrawing = false;

            if (isBoxSelecting) {
                float x1 = fminf(boxStart.x, boxEnd.x);
                float y1 = fminf(boxStart.y, boxEnd.y);
                float x2 = fmaxf(boxStart.x, boxEnd.x);
                float y2 = fmaxf(boxStart.y, boxEnd.y);
                if (x2-x1 > 5 || y2-y1 > 5) {
                    deselectAll();
                    for (int i = 0; i < (int)bodies.size(); i++) {
                        Vec2 c = bodies[i].center;
                        if (c.x >= x1 && c.x <= x2 && c.y >= y1 && c.y <= y2) {
                            selectBody(i);
                        }
                    }
                }
                isBoxSelecting = false;
            }
            isDragging = false;
        }

        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_z && (e.key.keysym.mod & KMOD_GUI)) {
                undo();
            }
            if (e.key.keysym.sym == SDLK_DELETE || e.key.keysym.sym == SDLK_BACKSPACE) {
                std::sort(selected.begin(), selected.end(), std::greater<int>());
                for (int idx : selected) bodies.erase(bodies.begin() + idx);
                selected.clear();
            }
        }
    }

    void update(float dt) {
        // Get drawable size for high-DPI
        int dw, dh;
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        screenW = dw; screenH = dh;

        // Physics
        if (simRunning) {
            int steps = std::max(1, (int)ceilf(dt / 0.005f));
            float subDt = dt / steps;
            for (int s = 0; s < steps; s++) {
                for (auto& b : bodies) {
                    if (!b.pinned) physicsStep(b, subDt, windVelocity());
                }
            }
        }

        // Particles always flow
        particles.update(dt, windPixelVel(), windDir(), windSpeed, screenW, screenH, bodies);
    }

    void renderBodies() {
        for (auto& body : bodies) {
            float r = body.selected ? 1.0f : 0.88f;
            float g = body.selected ? 0.67f : 0.88f;
            float b = body.selected ? 0.27f : 0.94f;
            float thick = body.selected ? 4.0f : 3.0f;

            for (auto& stroke : body.strokes) {
                // Stroke world position = body center + stroke center rotated by body angle
                float ca = cosf(body.angle), sa = sinf(body.angle);
                Vec2 sc = {stroke.center.x*ca - stroke.center.y*sa,
                           stroke.center.x*sa + stroke.center.y*ca};
                Vec2 worldPos = body.center + sc;

                lineRenderer.drawThickLine(stroke.points, worldPos, body.angle,
                                           thick, r, g, b, 1.0f, screenW, screenH);
            }
        }

        // Current drawing preview
        if (isDrawing && currentStroke.points.size() >= 2) {
            lineRenderer.drawThickLine(currentStroke.points, drawOrigin, 0,
                                       3.0f, 0.88f, 0.88f, 0.94f, 0.7f, screenW, screenH);
        }

        // Box select preview
        if (isBoxSelecting) {
            float x1 = fminf(boxStart.x, boxEnd.x);
            float y1 = fminf(boxStart.y, boxEnd.y);
            float x2 = fmaxf(boxStart.x, boxEnd.x);
            float y2 = fmaxf(boxStart.y, boxEnd.y);
            std::vector<Vec2> box = {{x1,y1},{x2,y1},{x2,y2},{x1,y2},{x1,y1}};
            lineRenderer.drawThickLine(box, {0,0}, 0, 1.0f, 0.4f, 0.6f, 1.0f, 0.5f, screenW, screenH);
        }
    }

    void renderGrid() {
        // Subtle grid using lines
        float alpha = 0.04f;
        for (float x = 0; x < screenW; x += 50) {
            std::vector<Vec2> line = {{x,0},{x,(float)screenH}};
            lineRenderer.drawThickLine(line, {0,0}, 0, 1.0f, 1,1,1, alpha, screenW, screenH);
        }
        for (float y = 0; y < screenH; y += 50) {
            std::vector<Vec2> line = {{0,y},{(float)screenW,y}};
            lineRenderer.drawThickLine(line, {0,0}, 0, 1.0f, 1,1,1, alpha, screenW, screenH);
        }
    }

    void renderUI() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Toolbar
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        if (ImGui::RadioButton("Draw", tool==0)) { tool=0; deselectAll(); }
        ImGui::SameLine();
        if (ImGui::RadioButton("Select", tool==1)) tool=1;
        ImGui::SameLine();
        if (ImGui::RadioButton("Stick", tool==2)) tool=2;

        ImGui::Separator();

        if (ImGui::Button("Undo (Cmd+Z)")) undo();
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            std::sort(selected.begin(), selected.end(), std::greater<int>());
            for (int idx : selected) bodies.erase(bodies.begin() + idx);
            selected.clear();
        }

        if (selected.size() >= 2) {
            ImGui::SameLine();
            if (ImGui::Button("Group into Body")) groupSelected();
        }

        ImGui::Separator();

        bool wasRunning = simRunning;
        if (ImGui::Button(simRunning ? "Pause" : "Play")) {
            simRunning = !simRunning;
            if (simRunning && !wasRunning) {
                for (auto& b : bodies) b.saveOrigin();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            simRunning = false;
            for (auto& b : bodies) b.resetToOrigin();
        }

        ImGui::Separator();

        ImGui::SliderFloat("Wind m/s", &windSpeed, 0, 80);
        ImGui::SliderFloat("Direction", &windAngleDeg, 0, 359);

        ImGui::Separator();

        if (!selected.empty()) {
            if (ImGui::Button("-5 deg")) {
                for (int i : selected) bodies[i].angle -= 5.0f * M_PI / 180.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("+5 deg")) {
                for (int i : selected) bodies[i].angle += 5.0f * M_PI / 180.0f;
            }
        }

        ImGui::Text("Particles: %d", particles.count);
        ImGui::Text("Bodies: %d", (int)bodies.size());

        ImGui::End();

        // Metadata panel for selected body
        if (selected.size() == 1) {
            Body& b = bodies[selected[0]];
            ImGui::SetNextWindowPos(ImVec2((float)screenW/ImGui::GetIO().DisplayFramebufferScale.x - 230, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(220, 0), ImGuiCond_FirstUseEver);
            ImGui::Begin("Metadata", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

            ImGui::Text("Angle: %.1f deg", fmodf(b.angle * 180.0f / M_PI, 360.0f));
            ImGui::Text("Radius: %.0f px", b.radius);
            ImGui::Text("Strokes: %d", (int)b.strokes.size());
            ImGui::Separator();
            ImGui::Text("Altitude: %.0f m (%.0f ft)", b.altitude, b.altitude * 3.281f);
            ImGui::Text("Air Density: %.3f kg/m3", b.airDensity);
            ImGui::Separator();
            ImGui::Text("Speed: %.1f m/s (%.0f mph)", b.speedMps, b.speedMps * 2.237f);
            ImGui::Text("Terminal V: %.1f m/s (%.0f mph)", b.terminalV, b.terminalV * 2.237f);
            ImGui::Text("Rel Wind: %.1f m/s", b.relWindSpeed);
            ImGui::Text("AoA: %.1f deg", b.relWindAngle);
            ImGui::Separator();
            ImGui::SliderFloat("Mass kg", &b.mass, 1, 150);
            ImGui::SliderFloat("Cd", &b.dragCoef, 0.1f, 1.5f);
            ImGui::SliderFloat("Area m2", &b.crossSection, 0.1f, 2.0f);
            ImGui::Checkbox("Pinned", &b.pinned);

            ImGui::End();
        } else if (selected.size() > 1) {
            ImGui::SetNextWindowPos(ImVec2((float)screenW/ImGui::GetIO().DisplayFramebufferScale.x - 230, 10), ImGuiCond_FirstUseEver);
            ImGui::Begin("Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("%d objects selected", (int)selected.size());
            if (ImGui::Button("Group into Single Body")) groupSelected();
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void render() {
        glViewport(0, 0, screenW, screenH);
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        renderGrid();

        // Particles
        glUseProgram(particleProgram);
        glUniform2f(glGetUniformLocation(particleProgram, "uScreen"), (float)screenW, (float)screenH);
        particles.uploadAndRender(screenW, screenH);

        renderBodies();
        renderUI();

        SDL_GL_SwapWindow(window);
    }

    void run() {
        init();
        bool running = true;
        Uint64 lastTick = SDL_GetPerformanceCounter();
        Uint64 freq = SDL_GetPerformanceFrequency();

        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL2_ProcessEvent(&e);
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
                handleEvent(e);
            }

            Uint64 now = SDL_GetPerformanceCounter();
            float dt = (float)(now - lastTick) / freq;
            dt = fminf(dt, 0.05f);
            lastTick = now;

            update(dt);
            render();
        }

        particles.cleanup();
        lineRenderer.cleanup();
        glDeleteProgram(particleProgram);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

int main(int, char**) {
    App app;
    app.run();
    return 0;
}
