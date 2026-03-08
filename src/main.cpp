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
    std::vector<Vec2> points; // local space relative to stroke offset
    Vec2 offset;              // offset from body center
    float radius = 0;

    void computeBounds() {
        if (points.empty()) return;
        // Compute centroid and recenter
        Vec2 sum{0,0};
        for (auto& p : points) sum += p;
        Vec2 centroid = sum * (1.0f / points.size());
        for (auto& p : points) p = p - centroid;
        offset = offset + centroid;
        radius = 0;
        for (auto& p : points) {
            float r = p.mag();
            if (r > radius) radius = r;
        }
        radius = fmaxf(radius, 10.0f);
    }
};

// ── Body ────────────────────────────────────────────────────────────────────

struct Body {
    std::vector<Stroke> strokes;
    Vec2 center;
    float angle = 0;
    float radius = 30;

    float mass = 80.0f;
    float dragCoef = 0.75f;
    float crossSection = 0.7f;
    float altitude = 4000.0f;
    Vec2 velocity{0,0};
    Vec2 acceleration{0,0};
    float speedMps = 0;
    float terminalV = 0;
    float relWindSpeed = 0;
    float relWindAngle = 0;
    float airDensity = 1.225f;

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
            float r = s.offset.mag() + s.radius;
            if (r > radius) radius = r;
        }
        radius = fmaxf(radius, 15.0f);
    }

    void recomputeCenter() {
        if (strokes.empty()) return;
        Vec2 sum{0,0};
        for (auto& s : strokes) sum += s.offset;
        Vec2 shift = sum * (1.0f / strokes.size());
        for (auto& s : strokes) s.offset = s.offset - shift;
        center = center + shift;
        computeRadius();
    }

    bool hitTest(Vec2 p) const {
        float c = cosf(-angle), s = sinf(-angle);
        Vec2 local = p - center;
        local = {local.x*c - local.y*s, local.x*s + local.y*c};
        for (auto& st : strokes) {
            Vec2 rel = local - st.offset;
            for (size_t i = 0; i < st.points.size(); i += 2) {
                float dx = rel.x - st.points[i].x;
                float dy = rel.y - st.points[i].y;
                if (dx*dx + dy*dy < 400.0f) return true;
            }
        }
        return false;
    }
};

// ── Potential Flow ──────────────────────────────────────────────────────────

struct FlowBody {
    float cx, cy, R2; // center and radius squared
};

Vec2 flowFieldAt(float px, float py, float wx, float wy,
                 const FlowBody* fb, int fbCount) {
    float vx = wx, vy = wy;
    for (int j = 0; j < fbCount; j++) {
        float dx = px - fb[j].cx;
        float dy = py - fb[j].cy;
        float r2 = dx*dx + dy*dy;
        if (r2 < fb[j].R2 * 1.05f) return {0, 0};
        float factor = fb[j].R2 / (r2 * r2);
        float dx2mdy2 = dx*dx - dy*dy;
        float dxdy2 = 2.0f * dx * dy;
        vx -= factor * (wx * dx2mdy2 + wy * dxdy2);
        vy -= factor * (-wy * dx2mdy2 + wx * dxdy2);
    }
    return {vx, vy};
}

// ── Particle System ─────────────────────────────────────────────────────────

static constexpr int MAX_PARTICLES = 30000;

struct Particles {
    float* x;
    float* y;
    float* life;
    float* uploadBuf; // persistent upload buffer
    int count = 0;
    float spawnAccum = 0;
    GLuint vao = 0, vbo = 0;

    void init() {
        x = new float[MAX_PARTICLES];
        y = new float[MAX_PARTICLES];
        life = new float[MAX_PARTICLES];
        uploadBuf = new float[MAX_PARTICLES * 3];

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * 3 * sizeof(float), nullptr, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), 0);
        glBindVertexArray(0);
    }

    void update(float dt, Vec2 wind, Vec2 windDir, float windSpeed, int screenW, int screenH,
                const FlowBody* fb, int fbCount) {
        if (windSpeed < 0.5f) { count = 0; return; }

        float w = (float)screenW, h = (float)screenH;

        // Spawn
        spawnAccum += windSpeed * 400.0f * dt;
        int toSpawn = (int)spawnAccum;
        spawnAccum -= toSpawn;

        float absDx = fabsf(windDir.x), absDy = fabsf(windDir.y);
        for (int i = 0; i < toSpawn && count < MAX_PARTICLES; i++) {
            int idx = count++;
            float r1 = (float)rand() / RAND_MAX;
            float r2 = (float)rand() / RAND_MAX;
            if (r1 < 0.08f) {
                x[idx] = r2 * w;
                y[idx] = ((float)rand()/RAND_MAX) * h;
            } else if (absDx > absDy) {
                x[idx] = windDir.x > 0 ? -r2*10.0f : w + r2*10.0f;
                y[idx] = r2 * h;
            } else {
                x[idx] = r2 * w;
                y[idx] = windDir.y > 0 ? -r2*10.0f : h + r2*10.0f;
            }
            life[idx] = 0.5f + r1 * 0.5f;
        }

        float decay = 0.25f * dt;
        float wx = wind.x, wy = wind.y;
        int alive = 0;

        for (int i = 0; i < count; i++) {
            float plife = life[i] - decay;
            if (plife <= 0 || x[i] < -50 || x[i] > w+50 || y[i] < -50 || y[i] > h+50) continue;

            Vec2 flow = flowFieldAt(x[i], y[i], wx, wy, fb, fbCount);
            x[alive] = x[i] + flow.x * dt;
            y[alive] = y[i] + flow.y * dt;
            life[alive] = plife;
            alive++;
        }
        count = alive;
    }

    void uploadAndRender() {
        if (count == 0) return;
        for (int i = 0; i < count; i++) {
            uploadBuf[i*3+0] = x[i];
            uploadBuf[i*3+1] = y[i];
            uploadBuf[i*3+2] = life[i];
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * 3 * sizeof(float), uploadBuf);
        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, count);
        glBindVertexArray(0);
    }

    void cleanup() {
        delete[] x; delete[] y; delete[] life; delete[] uploadBuf;
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
};

// ── Shaders ─────────────────────────────────────────────────────────────────

static const char* particleVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aData;
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

// ── Line Renderer (batched) ─────────────────────────────────────────────────

struct LineRenderer {
    GLuint vao = 0, vbo = 0;
    GLuint program = 0;
    std::vector<float> batch;

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

    void drawThickLine(const std::vector<Vec2>& pts, Vec2 offset, float angle,
                       float thickness, float r, float g, float b, float a,
                       int screenW, int screenH) {
        if (pts.size() < 2) return;

        float ca = cosf(angle), sa = sinf(angle);
        float half = thickness * 0.5f;

        batch.clear();
        for (size_t i = 0; i < pts.size(); i++) {
            Vec2 p = pts[i];
            Vec2 w = {offset.x + p.x*ca - p.y*sa, offset.y + p.x*sa + p.y*ca};

            Vec2 dir;
            if (pts.size() == 1) { dir = {1,0}; }
            else if (i == 0) {
                Vec2 p1 = pts[1];
                Vec2 w1 = {offset.x + p1.x*ca - p1.y*sa, offset.y + p1.x*sa + p1.y*ca};
                dir = (w1 - w).norm();
            } else if (i == pts.size()-1) {
                Vec2 pp = pts[i-1];
                Vec2 wp = {offset.x + pp.x*ca - pp.y*sa, offset.y + pp.x*sa + pp.y*ca};
                dir = (w - wp).norm();
            } else {
                Vec2 pp = pts[i-1], pn = pts[i+1];
                Vec2 wp = {offset.x + pp.x*ca - pp.y*sa, offset.y + pp.x*sa + pp.y*ca};
                Vec2 wn = {offset.x + pn.x*ca - pn.y*sa, offset.y + pn.x*sa + pn.y*ca};
                dir = (wn - wp).norm();
            }

            Vec2 n = dir.perp();
            batch.push_back(w.x + n.x*half); batch.push_back(w.y + n.y*half);
            batch.push_back(w.x - n.x*half); batch.push_back(w.y - n.y*half);
        }

        glUseProgram(program);
        glUniform2f(glGetUniformLocation(program, "uScreen"), (float)screenW, (float)screenH);
        glUniform4f(glGetUniformLocation(program, "uColor"), r, g, b, a);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, batch.size()*sizeof(float), batch.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, (int)(batch.size()/2));
        glBindVertexArray(0);
    }

    void cleanup() {
        glDeleteProgram(program);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
};

// ── Grid Renderer (single draw call) ────────────────────────────────────────

struct GridRenderer {
    GLuint vao = 0, vbo = 0;
    GLuint program = 0;
    int vertCount = 0;

    void init(GLuint prog) {
        program = prog;
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glBindVertexArray(0);
    }

    void rebuild(int w, int h) {
        std::vector<float> verts;
        for (float x = 0; x < w; x += 50) {
            verts.push_back(x); verts.push_back(0);
            verts.push_back(x); verts.push_back((float)h);
        }
        for (float y = 0; y < h; y += 50) {
            verts.push_back(0);      verts.push_back(y);
            verts.push_back((float)w); verts.push_back(y);
        }
        vertCount = (int)(verts.size() / 2);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    }

    void render(int w, int h) {
        if (vertCount == 0) return;
        glUseProgram(program);
        glUniform2f(glGetUniformLocation(program, "uScreen"), (float)w, (float)h);
        glUniform4f(glGetUniformLocation(program, "uColor"), 1, 1, 1, 0.04f);
        glBindVertexArray(vao);
        glDrawArrays(GL_LINES, 0, vertCount);
        glBindVertexArray(0);
    }

    void cleanup() {
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
};

// ── Physics ─────────────────────────────────────────────────────────────────

static constexpr float GRAVITY = 9.81f;
static constexpr float SEA_LEVEL_RHO = 1.225f;
static constexpr float PX_PER_METER = 10.0f;

float airDensityAt(float alt) {
    return SEA_LEVEL_RHO * expf(-alt / 8500.0f);
}

void physicsStep(Body& body, float dt, Vec2 windVelocity) {
    if (body.pinned) return;
    float rho = airDensityAt(body.altitude);
    body.airDensity = rho;

    Vec2 relWind = windVelocity - body.velocity;
    body.relWindSpeed = relWind.mag();

    Vec2 bodyDir = Vec2{1,0}.rotate(body.angle);
    if (body.relWindSpeed > 0.01f) {
        float d = fmaxf(-1.0f, fminf(1.0f, bodyDir.dot(relWind.norm())));
        body.relWindAngle = acosf(d) * 180.0f / M_PI;
    } else {
        body.relWindAngle = 0;
    }

    float aoaRad = body.relWindAngle * M_PI / 180.0f;
    float cdMul = 0.6f + 0.4f * powf(fabsf(sinf(aoaRad)), 1.2f);
    float effCd = body.dragCoef * cdMul;
    float effArea = body.crossSection * (0.5f + 0.5f * fabsf(sinf(aoaRad)));

    float v2 = body.relWindSpeed * body.relWindSpeed;
    float dragMag = 0.5f * rho * v2 * effCd * effArea;
    Vec2 dragForce = body.relWindSpeed > 0.01f ? relWind.norm() * dragMag : Vec2{0,0};

    float liftCoef = 0.4f * sinf(2.0f * aoaRad);
    float liftMag = 0.5f * rho * v2 * fabsf(liftCoef) * effArea;
    Vec2 liftForce{0,0};
    if (body.relWindSpeed > 0.01f && fabsf(liftCoef) > 0.001f) {
        Vec2 wn = relWind.norm();
        float cross = bodyDir.x * wn.y - bodyDir.y * wn.x;
        liftForce = wn.perp() * (liftMag * (cross >= 0 ? 1.0f : -1.0f));
    }

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
    SDL_Window* window = nullptr;
    SDL_GLContext gl;
    int screenW = 1400, screenH = 900;
    int lastGridW = 0, lastGridH = 0;

    GLuint particleProgram = 0;
    Particles particles;
    LineRenderer lineRenderer;
    GridRenderer gridRenderer;

    std::vector<Body> bodies;
    std::vector<FlowBody> flowBodies; // cached per frame

    bool isDrawing = false;
    Stroke currentStroke;
    Vec2 drawOrigin;

    std::vector<int> selected;
    bool isDragging = false;
    Vec2 dragStart;
    bool isBoxSelecting = false;
    Vec2 boxStart, boxEnd;

    int tool = 0; // 0=draw, 1=select, 2=stick
    bool simRunning = false;
    float windSpeed = 30.0f;
    float windAngleDeg = 270.0f;

    Vec2 windDir() const {
        float a = windAngleDeg * M_PI / 180.0f;
        return {cosf(a), sinf(a)};
    }
    Vec2 windVelocity() const { return windDir() * windSpeed; }
    Vec2 windPixelVel() const { return windDir() * (windSpeed * 10.0f); }

    float dpiScale() const {
        int dw, dh, ww, wh;
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        SDL_GetWindowSize(window, &ww, &wh);
        return (float)dw / (float)ww;
    }

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
        SDL_GL_SetSwapInterval(1);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_MULTISAMPLE);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4;
        style.FrameRounding = 2;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.09f, 0.95f);

        ImGui_ImplSDL2_InitForOpenGL(window, gl);
        ImGui_ImplOpenGL3_Init("#version 330");

        particleProgram = createProgram(particleVertSrc, particleFragSrc);
        particles.init();
        lineRenderer.init();
        gridRenderer.init(lineRenderer.program);
    }

    void deselectAll() {
        for (int i : selected) if (i < (int)bodies.size()) bodies[i].selected = false;
        selected.clear();
    }

    void selectBody(int idx) {
        bodies[idx].selected = true;
        if (std::find(selected.begin(), selected.end(), idx) == selected.end())
            selected.push_back(idx);
    }

    void groupSelected() {
        if (selected.size() < 2) return;
        Body merged;
        merged.mass = 0;
        merged.crossSection = 0;
        for (int idx : selected) {
            Body& b = bodies[idx];
            float ca = cosf(b.angle), sa = sinf(b.angle);
            for (auto& s : b.strokes) {
                Stroke ns = s;
                // Transform offset by body angle, then add body center
                Vec2 worldOff = {
                    b.center.x + s.offset.x * ca - s.offset.y * sa,
                    b.center.y + s.offset.x * sa + s.offset.y * ca
                };
                ns.offset = worldOff;
                for (auto& p : ns.points) p = p.rotate(b.angle);
                merged.strokes.push_back(ns);
            }
            merged.mass += b.mass;
            merged.crossSection += b.crossSection;
        }

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
        bodies.pop_back();
        selected.erase(std::remove_if(selected.begin(), selected.end(),
            [&](int i){ return i >= (int)bodies.size(); }), selected.end());
    }

    void deleteSelected() {
        std::sort(selected.begin(), selected.end(), std::greater<int>());
        for (int idx : selected) bodies.erase(bodies.begin() + idx);
        selected.clear();
    }

    void handleEvent(SDL_Event& e) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse || io.WantCaptureKeyboard) return;

        float s = dpiScale();

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            float fx = e.button.x * s, fy = e.button.y * s;

            if (tool == 0) {
                isDrawing = true;
                drawOrigin = {fx, fy};
                currentStroke = Stroke{};
                currentStroke.offset = {0, 0};
                currentStroke.points.push_back({0, 0});
            } else if (tool == 1) {
                for (int idx : selected) {
                    if (bodies[idx].hitTest({fx, fy})) {
                        isDragging = true;
                        dragStart = {fx, fy};
                        return;
                    }
                }
                deselectAll();
                for (int i = (int)bodies.size()-1; i >= 0; i--) {
                    if (bodies[i].hitTest({fx, fy})) {
                        selectBody(i);
                        isDragging = true;
                        dragStart = {fx, fy};
                        return;
                    }
                }
                isBoxSelecting = true;
                boxStart = boxEnd = {fx, fy};
            } else if (tool == 2) {
                Body b;
                float h = 80.0f, headR = h*0.15f;
                Stroke head; head.offset = {0, 0};
                for (int i = 0; i <= 20; i++) {
                    float a = (float)i / 20.0f * 2.0f * M_PI;
                    head.points.push_back({cosf(a)*headR, -h/2+headR + sinf(a)*headR});
                }
                head.computeBounds();

                Stroke bodyLine; bodyLine.offset = {0, 0};
                bodyLine.points.push_back({0, -h/2 + headR*2});
                bodyLine.points.push_back({0, -h/2 + headR*2 + h*0.4f});
                bodyLine.computeBounds();

                float armY = -h/2 + headR*2 + h*0.12f;
                Stroke arms; arms.offset = {0, 0};
                arms.points.push_back({-h*0.25f, armY});
                arms.points.push_back({h*0.25f, armY});
                arms.computeBounds();

                float hipY = -h/2 + headR*2 + h*0.4f;
                Stroke legL; legL.offset = {0, 0};
                legL.points.push_back({0, hipY});
                legL.points.push_back({-h*0.18f, hipY + h*0.3f});
                legL.computeBounds();
                Stroke legR; legR.offset = {0, 0};
                legR.points.push_back({0, hipY});
                legR.points.push_back({h*0.18f, hipY + h*0.3f});
                legR.computeBounds();

                b.strokes = {head, bodyLine, arms, legL, legR};
                b.center = {fx, fy};
                b.computeRadius();
                b.mass = 80.0f;
                b.dragCoef = 0.8f;
                b.crossSection = 0.7f;
                b.saveOrigin();
                bodies.push_back(b);
            }
        }

        if (e.type == SDL_MOUSEMOTION) {
            float fx = e.motion.x * s, fy = e.motion.y * s;

            if (isDrawing) {
                Vec2 rel = {fx - drawOrigin.x, fy - drawOrigin.y};
                auto& pts = currentStroke.points;
                Vec2 last = pts.back();
                if ((rel - last).mag2() > 16.0f) pts.push_back(rel);
            }
            if (isDragging && !selected.empty()) {
                Vec2 cur = {fx, fy};
                Vec2 delta = cur - dragStart;
                for (int idx : selected) bodies[idx].center += delta;
                dragStart = cur;
            }
            if (isBoxSelecting) boxEnd = {fx, fy};
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            if (isDrawing && currentStroke.points.size() >= 3) {
                currentStroke.computeBounds();
                Body b;
                // offset is now the centroid shift from computeBounds
                // body center = drawOrigin + offset
                b.center = drawOrigin + currentStroke.offset;
                currentStroke.offset = {0, 0}; // stroke is at body center
                b.strokes.push_back(currentStroke);
                b.radius = currentStroke.radius;
                b.mass = 5.0f;
                b.crossSection = fmaxf(b.radius / 100.0f, 0.1f);
                b.dragCoef = 0.5f;
                b.saveOrigin();
                bodies.push_back(b);
            }
            isDrawing = false;

            if (isBoxSelecting) {
                float x1 = fminf(boxStart.x, boxEnd.x), y1 = fminf(boxStart.y, boxEnd.y);
                float x2 = fmaxf(boxStart.x, boxEnd.x), y2 = fmaxf(boxStart.y, boxEnd.y);
                if (x2-x1 > 5 || y2-y1 > 5) {
                    deselectAll();
                    for (int i = 0; i < (int)bodies.size(); i++) {
                        Vec2 c = bodies[i].center;
                        if (c.x >= x1 && c.x <= x2 && c.y >= y1 && c.y <= y2)
                            selectBody(i);
                    }
                }
                isBoxSelecting = false;
            }
            isDragging = false;
        }

        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_z && (e.key.keysym.mod & KMOD_GUI)) undo();
            if (e.key.keysym.sym == SDLK_DELETE || e.key.keysym.sym == SDLK_BACKSPACE) deleteSelected();
        }
    }

    void update(float dt) {
        int dw, dh;
        SDL_GL_GetDrawableSize(window, &dw, &dh);
        screenW = dw; screenH = dh;

        // Rebuild grid if size changed
        if (screenW != lastGridW || screenH != lastGridH) {
            gridRenderer.rebuild(screenW, screenH);
            lastGridW = screenW; lastGridH = screenH;
        }

        if (simRunning) {
            int steps = std::max(1, (int)ceilf(dt / 0.005f));
            float subDt = dt / steps;
            for (int s = 0; s < steps; s++) {
                for (auto& b : bodies) {
                    if (!b.pinned) physicsStep(b, subDt, windVelocity());
                }
            }
        }

        // Build flow bodies for particles
        flowBodies.clear();
        for (auto& b : bodies) {
            b.computeRadius();
            flowBodies.push_back({b.center.x, b.center.y, b.radius * b.radius});
        }

        particles.update(dt, windPixelVel(), windDir(), windSpeed, screenW, screenH,
                         flowBodies.data(), (int)flowBodies.size());
    }

    void renderBodies() {
        for (auto& body : bodies) {
            float r = body.selected ? 1.0f : 0.88f;
            float g = body.selected ? 0.67f : 0.88f;
            float b = body.selected ? 0.27f : 0.94f;
            float thick = body.selected ? 4.0f : 3.0f;

            float ca = cosf(body.angle), sa = sinf(body.angle);
            for (auto& stroke : body.strokes) {
                Vec2 worldPos = body.center + Vec2{
                    stroke.offset.x*ca - stroke.offset.y*sa,
                    stroke.offset.x*sa + stroke.offset.y*ca
                };
                lineRenderer.drawThickLine(stroke.points, worldPos, body.angle,
                                           thick, r, g, b, 1.0f, screenW, screenH);
            }
        }

        if (isDrawing && currentStroke.points.size() >= 2) {
            lineRenderer.drawThickLine(currentStroke.points, drawOrigin, 0,
                                       3.0f, 0.88f, 0.88f, 0.94f, 0.7f, screenW, screenH);
        }

        if (isBoxSelecting) {
            float x1 = fminf(boxStart.x, boxEnd.x), y1 = fminf(boxStart.y, boxEnd.y);
            float x2 = fmaxf(boxStart.x, boxEnd.x), y2 = fmaxf(boxStart.y, boxEnd.y);
            std::vector<Vec2> box = {{x1,y1},{x2,y1},{x2,y2},{x1,y2},{x1,y1}};
            lineRenderer.drawThickLine(box, {0,0}, 0, 1.0f, 0.4f, 0.6f, 1.0f, 0.5f, screenW, screenH);
        }
    }

    void renderUI() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Tools", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        if (ImGui::RadioButton("Draw", tool==0)) { tool=0; deselectAll(); }
        ImGui::SameLine();
        if (ImGui::RadioButton("Select", tool==1)) tool=1;
        ImGui::SameLine();
        if (ImGui::RadioButton("Stick", tool==2)) tool=2;

        ImGui::Separator();
        if (ImGui::Button("Undo")) undo();
        ImGui::SameLine();
        if (ImGui::Button("Delete")) deleteSelected();
        if (selected.size() >= 2) {
            ImGui::SameLine();
            if (ImGui::Button("Group")) groupSelected();
        }

        ImGui::Separator();
        if (ImGui::Button(simRunning ? "Pause" : "Play")) {
            simRunning = !simRunning;
            if (simRunning) for (auto& b : bodies) b.saveOrigin();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            simRunning = false;
            for (auto& b : bodies) b.resetToOrigin();
        }

        ImGui::Separator();
        ImGui::SliderFloat("Wind m/s", &windSpeed, 0, 80);
        ImGui::SliderFloat("Direction", &windAngleDeg, 0, 359);

        if (!selected.empty()) {
            ImGui::Separator();
            if (ImGui::Button("-5 deg")) { for (int i : selected) bodies[i].angle -= 5.0f*M_PI/180.0f; }
            ImGui::SameLine();
            if (ImGui::Button("+5 deg")) { for (int i : selected) bodies[i].angle += 5.0f*M_PI/180.0f; }
        }

        ImGui::Text("Particles: %d | Bodies: %d", particles.count, (int)bodies.size());
        ImGui::End();

        if (selected.size() == 1) {
            Body& b = bodies[selected[0]];
            float panelX = (float)screenW / dpiScale() - 230;
            ImGui::SetNextWindowPos(ImVec2(panelX, 10), ImGuiCond_FirstUseEver);
            ImGui::Begin("Metadata", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Angle: %.1f deg", fmodf(b.angle*180.0f/M_PI, 360.0f));
            ImGui::Text("Radius: %.0f px", b.radius);
            ImGui::Separator();
            ImGui::Text("Altitude: %.0f m (%.0f ft)", b.altitude, b.altitude*3.281f);
            ImGui::Text("Air: %.3f kg/m3", b.airDensity);
            ImGui::Separator();
            ImGui::Text("Speed: %.1f m/s (%.0f mph)", b.speedMps, b.speedMps*2.237f);
            ImGui::Text("Terminal: %.1f m/s (%.0f mph)", b.terminalV, b.terminalV*2.237f);
            ImGui::Text("Rel Wind: %.1f m/s", b.relWindSpeed);
            ImGui::Text("AoA: %.1f deg", b.relWindAngle);
            ImGui::Separator();
            ImGui::SliderFloat("Mass", &b.mass, 1, 150);
            ImGui::SliderFloat("Cd", &b.dragCoef, 0.1f, 1.5f);
            ImGui::SliderFloat("Area", &b.crossSection, 0.1f, 2.0f);
            ImGui::Checkbox("Pinned", &b.pinned);
            ImGui::End();
        } else if (selected.size() > 1) {
            ImGui::Begin("Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("%d selected", (int)selected.size());
            if (ImGui::Button("Group into Body")) groupSelected();
            ImGui::End();
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void render() {
        glViewport(0, 0, screenW, screenH);
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        gridRenderer.render(screenW, screenH);

        glUseProgram(particleProgram);
        glUniform2f(glGetUniformLocation(particleProgram, "uScreen"), (float)screenW, (float)screenH);
        particles.uploadAndRender();

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
            float dt = fminf((float)(now - lastTick) / freq, 0.05f);
            lastTick = now;

            update(dt);
            render();
        }

        particles.cleanup();
        lineRenderer.cleanup();
        gridRenderer.cleanup();
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
