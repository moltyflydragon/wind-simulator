#include <SDL.h>
#include <OpenGL/gl3.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

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

// ── Stroke & Body ───────────────────────────────────────────────────────────

struct Stroke {
    std::vector<Vec2> points;
    Vec2 offset; // from body center
    float radius = 0;

    void computeBounds() {
        if (points.empty()) return;
        Vec2 sum{0,0};
        for (auto& p : points) sum += p;
        Vec2 c = sum * (1.0f / points.size());
        for (auto& p : points) p = p - c;
        offset = offset + c;
        radius = 0;
        for (auto& p : points) { float r = p.mag(); if (r > radius) radius = r; }
        radius = fmaxf(radius, 10.0f);
    }
};

struct Body {
    std::vector<Stroke> strokes;
    Vec2 center;
    float angle = 0, radius = 30;
    float mass = 80, dragCoef = 0.75f, crossSection = 0.7f, altitude = 4000;
    Vec2 velocity{0,0}, acceleration{0,0};
    float speedMps = 0, terminalV = 0, relWindSpeed = 0, relWindAngle = 0, airDensity = 1.225f;
    bool selected = false, pinned = false;
    Vec2 originCenter; float originAngle = 0;

    void saveOrigin() { originCenter = center; originAngle = angle; }
    void resetToOrigin() { center = originCenter; angle = originAngle; velocity = {0,0}; altitude = 4000; speedMps = 0; }
    void computeRadius() {
        radius = 0;
        for (auto& s : strokes) { float r = s.offset.mag() + s.radius; if (r > radius) radius = r; }
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
                float dx = rel.x - st.points[i].x, dy = rel.y - st.points[i].y;
                if (dx*dx + dy*dy < 400.0f) return true;
            }
        }
        return false;
    }
};

// ── World Segments (precomputed each frame for particle interaction) ─────────
// Particles deflect against actual stroke line segments, not bounding circles.
// This gives detailed flow: wind wrapping around the head, between arms, etc.

struct Seg { float ax, ay, bx, by, nx, ny; }; // endpoints + outward normal

static std::vector<Seg> worldSegs;
static std::vector<float> bodyBoundX, bodyBoundY, bodyBoundR2; // broad phase

void buildWorldSegs(const std::vector<Body>& bodies) {
    worldSegs.clear();
    bodyBoundX.clear(); bodyBoundY.clear(); bodyBoundR2.clear();
    for (auto& body : bodies) {
        bodyBoundX.push_back(body.center.x);
        bodyBoundY.push_back(body.center.y);
        float R = body.radius + 20; // expand for influence zone
        bodyBoundR2.push_back(R * R);

        float ca = cosf(body.angle), sa = sinf(body.angle);
        for (auto& st : body.strokes) {
            Vec2 wo = body.center + Vec2{st.offset.x*ca - st.offset.y*sa, st.offset.x*sa + st.offset.y*ca};
            for (size_t j = 0; j + 1 < st.points.size(); j++) {
                Vec2 a = st.points[j].rotate(body.angle);
                Vec2 b = st.points[j+1].rotate(body.angle);
                Vec2 wa = {wo.x + a.x, wo.y + a.y};
                Vec2 wb = {wo.x + b.x, wo.y + b.y};
                // Segment normal (perpendicular, pick consistent side)
                Vec2 d = wb - wa;
                float len = d.mag();
                if (len < 0.5f) continue;
                Vec2 n = {-d.y / len, d.x / len}; // left normal
                worldSegs.push_back({wa.x, wa.y, wb.x, wb.y, n.x, n.y});
            }
        }
    }
}

// Closest point on segment AB to point P, returns distance squared
float closestPointOnSeg(float px, float py, float ax, float ay, float bx, float by,
                        float& outX, float& outY) {
    float dx = bx - ax, dy = by - ay;
    float len2 = dx*dx + dy*dy;
    if (len2 < 0.01f) { outX = ax; outY = ay; float ex=px-ax,ey=py-ay; return ex*ex+ey*ey; }
    float t = ((px-ax)*dx + (py-ay)*dy) / len2;
    t = fmaxf(0, fminf(1, t));
    outX = ax + t * dx;
    outY = ay + t * dy;
    float ex = px - outX, ey = py - outY;
    return ex*ex + ey*ey;
}

// ── Particles ───────────────────────────────────────────────────────────────
// Air is everywhere. Particles deflect against actual stroke geometry.

static constexpr int MAX_P = 35000;
static constexpr float SEG_INFLUENCE = 25.0f; // pixels: how far segments affect particles
static constexpr float SEG_INF2 = SEG_INFLUENCE * SEG_INFLUENCE;

struct Particles {
    float* px; float* py; float* life;
    float* buf;
    int count = 0;
    float spawnAccum = 0;
    GLuint vao = 0, vbo = 0;

    void init() {
        px = new float[MAX_P]; py = new float[MAX_P];
        life = new float[MAX_P]; buf = new float[MAX_P * 3];
        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, MAX_P * 3 * sizeof(float), nullptr, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), 0);
        glBindVertexArray(0);
    }

    void update(float dt, float wx, float wy, float windSpeed, int sw, int sh) {
        if (windSpeed < 0.5f) { count = 0; return; }

        float w = (float)sw, h = (float)sh;
        float wdx = wx / (windSpeed * 10.0f + 0.001f);
        float wdy = wy / (windSpeed * 10.0f + 0.001f);
        int nSegs = (int)worldSegs.size();
        int nBodies = (int)bodyBoundX.size();
        const Seg* segs = worldSegs.data();

        // Spawn
        spawnAccum += windSpeed * 500.0f * dt;
        int toSpawn = (int)spawnAccum; spawnAccum -= toSpawn;
        for (int i = 0; i < toSpawn && count < MAX_P; i++) {
            int idx = count++;
            float r = (float)rand()/RAND_MAX;
            if (r < 0.3f) {
                px[idx] = ((float)rand()/RAND_MAX) * w;
                py[idx] = ((float)rand()/RAND_MAX) * h;
            } else if (fabsf(wdx) > fabsf(wdy)) {
                px[idx] = wdx > 0 ? ((float)rand()/RAND_MAX)*-15 : w+((float)rand()/RAND_MAX)*15;
                py[idx] = ((float)rand()/RAND_MAX) * h;
            } else {
                px[idx] = ((float)rand()/RAND_MAX) * w;
                py[idx] = wdy > 0 ? ((float)rand()/RAND_MAX)*-15 : h+((float)rand()/RAND_MAX)*15;
            }
            life[idx] = 0.4f + ((float)rand()/RAND_MAX) * 0.6f;
        }

        float decay = 0.2f * dt;
        int alive = 0;

        for (int i = 0; i < count; i++) {
            float pl = life[i] - decay;
            if (pl <= 0 || px[i] < -60 || px[i] > w+60 || py[i] < -60 || py[i] > h+60) continue;

            float vx = wx, vy = wy;
            float ppx = px[i], ppy = py[i];

            // Broad phase: check if near any body
            bool nearBody = false;
            for (int b = 0; b < nBodies; b++) {
                float dx = ppx - bodyBoundX[b], dy = ppy - bodyBoundY[b];
                if (dx*dx + dy*dy < bodyBoundR2[b]) { nearBody = true; break; }
            }

            if (nearBody) {
                // Narrow phase: check against actual segments
                float closestD2 = 1e9f;
                float closestNx = 0, closestNy = 0;
                float closestCx = 0, closestCy = 0;

                for (int s = 0; s < nSegs; s++) {
                    float cx, cy;
                    float d2 = closestPointOnSeg(ppx, ppy, segs[s].ax, segs[s].ay,
                                                  segs[s].bx, segs[s].by, cx, cy);
                    if (d2 < closestD2) {
                        closestD2 = d2;
                        closestCx = cx; closestCy = cy;
                        closestNx = segs[s].nx; closestNy = segs[s].ny;
                    }
                }

                if (closestD2 < SEG_INF2 && closestD2 > 0.1f) {
                    float dist = sqrtf(closestD2);
                    // Normal from segment surface toward particle
                    float toPx = ppx - closestCx, toPy = ppy - closestCy;
                    float toLen = sqrtf(toPx*toPx + toPy*toPy);
                    if (toLen > 0.1f) {
                        float nx = toPx / toLen, ny = toPy / toLen;
                        float strength = 1.0f - dist / SEG_INFLUENCE; // 1 at surface, 0 at edge

                        // Reflect wind component going into the surface
                        float into = vx * (-nx) + vy * (-ny);
                        if (into > 0) {
                            vx += nx * into * strength * 1.2f;
                            vy += ny * into * strength * 1.2f;
                        }

                        // Very close: add turbulence (boundary layer)
                        if (dist < 8.0f) {
                            float turb = (8.0f - dist) / 8.0f;
                            vx += ((float)rand()/RAND_MAX - 0.5f) * windSpeed * turb * 3.0f;
                            vy += ((float)rand()/RAND_MAX - 0.5f) * windSpeed * turb * 3.0f;
                        }

                        // Slight acceleration around edges (Bernoulli)
                        float tangent = vx * (-ny) + vy * nx; // tangential component
                        if (strength > 0.3f) {
                            vx += (-ny) * fabsf(tangent) * strength * 0.15f;
                            vy += nx * fabsf(tangent) * strength * 0.15f;
                        }
                    }
                }
            }

            px[alive] = ppx + vx * dt;
            py[alive] = ppy + vy * dt;
            life[alive] = pl;
            alive++;
        }
        count = alive;
    }

    void render() {
        if (count == 0) return;
        for (int i = 0; i < count; i++) {
            buf[i*3] = px[i]; buf[i*3+1] = py[i]; buf[i*3+2] = life[i];
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * 3 * sizeof(float), buf);
        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, count);
        glBindVertexArray(0);
    }

    void cleanup() { delete[] px; delete[] py; delete[] life; delete[] buf; glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao); }
};

// ── Shaders ─────────────────────────────────────────────────────────────────

static const char* pVert = R"(#version 330 core
layout(location=0) in vec3 a;
uniform vec2 uS;
out float vL;
void main(){
    gl_Position = vec4(a.x/uS.x*2.0-1.0, 1.0-a.y/uS.y*2.0, 0, 1);
    gl_PointSize = 1.5;
    vL = a.z;
})";
static const char* pFrag = R"(#version 330 core
in float vL;
out vec4 f;
void main(){ f = vec4(0.55, 0.72, 1.0, vL * 0.45); })";

static const char* lVert = R"(#version 330 core
layout(location=0) in vec2 a;
uniform vec2 uS;
uniform vec4 uC;
out vec4 vC;
void main(){
    gl_Position = vec4(a.x/uS.x*2.0-1.0, 1.0-a.y/uS.y*2.0, 0, 1);
    vC = uC;
})";
static const char* lFrag = R"(#version 330 core
in vec4 vC; out vec4 f; void main(){ f = vC; })";

GLuint mkShader(GLenum t, const char* s) {
    GLuint sh = glCreateShader(t); glShaderSource(sh,1,&s,nullptr); glCompileShader(sh);
    int ok; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if(!ok){char b[512]; glGetShaderInfoLog(sh,512,nullptr,b); printf("Shader: %s\n",b);}
    return sh;
}
GLuint mkProg(const char* v, const char* f) {
    GLuint vs=mkShader(GL_VERTEX_SHADER,v), fs=mkShader(GL_FRAGMENT_SHADER,f);
    GLuint p=glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs); return p;
}

// ── Batched Line Renderer ───────────────────────────────────────────────────
// All strokes collected into one VBO, drawn in one call per color.

struct Lines {
    GLuint vao=0, vbo=0, prog=0;
    std::vector<float> verts;

    void init() {
        prog = mkProg(lVert, lFrag);
        glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
        glBindVertexArray(0);
    }

    void begin() { verts.clear(); }

    void addStrip(const std::vector<Vec2>& pts, Vec2 off, float ang, float thick) {
        if (pts.size() < 2) return;
        float ca=cosf(ang), sa=sinf(ang), half=thick*0.5f;

        // Degenerate triangle to separate from previous strip
        if (!verts.empty()) {
            // Repeat last vertex
            verts.push_back(verts[verts.size()-2]);
            verts.push_back(verts[verts.size()-2]);
            // And first of this strip (will add below, then repeat)
        }

        bool needFirst = !verts.empty(); // need to repeat first vert for degenerate
        bool firstDone = false;

        for (size_t i = 0; i < pts.size(); i++) {
            Vec2 p = pts[i];
            Vec2 w = {off.x + p.x*ca - p.y*sa, off.y + p.x*sa + p.y*ca};

            Vec2 dir;
            if (i == 0) {
                Vec2 p1=pts[1];
                dir = (Vec2{off.x+p1.x*ca-p1.y*sa, off.y+p1.x*sa+p1.y*ca} - w).norm();
            } else if (i == pts.size()-1) {
                Vec2 pp=pts[i-1];
                dir = (w - Vec2{off.x+pp.x*ca-pp.y*sa, off.y+pp.x*sa+pp.y*ca}).norm();
            } else {
                Vec2 pp=pts[i-1], pn=pts[i+1];
                dir = (Vec2{off.x+pn.x*ca-pn.y*sa, off.y+pn.x*sa+pn.y*ca}
                     - Vec2{off.x+pp.x*ca-pp.y*sa, off.y+pp.x*sa+pp.y*ca}).norm();
            }

            Vec2 n = dir.perp();
            float ax = w.x + n.x*half, ay = w.y + n.y*half;
            float bx = w.x - n.x*half, by = w.y - n.y*half;

            if (needFirst && !firstDone) {
                verts.push_back(ax); verts.push_back(ay); // degenerate connector
                firstDone = true;
            }

            verts.push_back(ax); verts.push_back(ay);
            verts.push_back(bx); verts.push_back(by);
        }
    }

    void draw(float r, float g, float b, float a, int sw, int sh) {
        if (verts.empty()) return;
        glUseProgram(prog);
        glUniform2f(glGetUniformLocation(prog,"uS"), (float)sw, (float)sh);
        glUniform4f(glGetUniformLocation(prog,"uC"), r, g, b, a);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, (int)(verts.size()/2));
        glBindVertexArray(0);
    }

    void cleanup() { glDeleteProgram(prog); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao); }
};

// ── Grid ────────────────────────────────────────────────────────────────────

struct Grid {
    GLuint vao=0, vbo=0; int n=0; int lastW=0, lastH=0;
    void init() {
        glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
        glBindVertexArray(0);
    }
    void rebuild(int w, int h) {
        if (w==lastW && h==lastH) return;
        lastW=w; lastH=h;
        std::vector<float> v;
        for(float x=0;x<w;x+=50){v.push_back(x);v.push_back(0);v.push_back(x);v.push_back((float)h);}
        for(float y=0;y<h;y+=50){v.push_back(0);v.push_back(y);v.push_back((float)w);v.push_back(y);}
        n=(int)(v.size()/2);
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(float),v.data(),GL_STATIC_DRAW);
    }
    void render(GLuint prog, int w, int h) {
        if(!n)return;
        glUseProgram(prog);
        glUniform2f(glGetUniformLocation(prog,"uS"),(float)w,(float)h);
        glUniform4f(glGetUniformLocation(prog,"uC"),1,1,1,0.04f);
        glBindVertexArray(vao); glDrawArrays(GL_LINES,0,n); glBindVertexArray(0);
    }
    void cleanup(){glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);}
};

// ── Physics ─────────────────────────────────────────────────────────────────

void physicsStep(Body& b, float dt, Vec2 wv) {
    if (b.pinned) return;
    float rho = 1.225f * expf(-b.altitude / 8500.0f);
    b.airDensity = rho;
    Vec2 rw = wv - b.velocity; b.relWindSpeed = rw.mag();
    Vec2 bd = Vec2{1,0}.rotate(b.angle);
    if (b.relWindSpeed > 0.01f) {
        float d = fmaxf(-1.0f, fminf(1.0f, bd.dot(rw.norm())));
        b.relWindAngle = acosf(d) * 180.0f / M_PI;
    } else b.relWindAngle = 0;
    float ar = b.relWindAngle * M_PI / 180.0f;
    float ecd = b.dragCoef * (0.6f + 0.4f*powf(fabsf(sinf(ar)),1.2f));
    float ea = b.crossSection * (0.5f + 0.5f*fabsf(sinf(ar)));
    float v2 = b.relWindSpeed * b.relWindSpeed;
    float dm = 0.5f*rho*v2*ecd*ea;
    Vec2 df = b.relWindSpeed > 0.01f ? rw.norm()*dm : Vec2{0,0};
    float lc = 0.4f*sinf(2*ar), lm = 0.5f*rho*v2*fabsf(lc)*ea;
    Vec2 lf{0,0};
    if (b.relWindSpeed > 0.01f && fabsf(lc) > 0.001f) {
        Vec2 wn=rw.norm(); float cr=bd.x*wn.y-bd.y*wn.x;
        lf = wn.perp()*(lm*(cr>=0?1:-1));
    }
    Vec2 tot = Vec2{0,b.mass*9.81f}+df+lf;
    b.acceleration = tot*(1/b.mass); b.velocity += b.acceleration*dt;
    b.speedMps = b.velocity.mag();
    b.altitude -= b.velocity.y*dt; if(b.altitude<0)b.altitude=0;
    b.terminalV = sqrtf(2*b.mass*9.81f/(rho*ecd*ea));
    b.center += b.velocity*(dt*10.0f);
}

// ── App ─────────────────────────────────────────────────────────────────────

struct App {
    SDL_Window* win=nullptr; SDL_GLContext gl;
    int sw=1400, sh=900;
    GLuint pProg=0;
    Particles parts;
    Lines lines;
    Grid grid;

    std::vector<Body> bodies;
    bool isDrawing=false; Stroke curStroke; Vec2 drawOrig;
    std::vector<int> sel; bool isDrag=false; Vec2 dragSt;
    bool isBox=false; Vec2 boxA, boxB;
    int tool=0; bool simOn=false;
    float windSpd=30, windAng=270;

    Vec2 wDir() const { float a=windAng*M_PI/180; return {cosf(a),sinf(a)}; }
    Vec2 wVel() const { return wDir()*windSpd; }
    Vec2 wPx() const { return wDir()*(windSpd*10); }
    float dpi() const { int dw,ww,dh,wh; SDL_GL_GetDrawableSize(win,&dw,&dh); SDL_GetWindowSize(win,&ww,&wh); return (float)dw/ww; }

    void init() {
        SDL_Init(SDL_INIT_VIDEO);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        win = SDL_CreateWindow("Wind Simulator",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
            sw,sh,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        gl = SDL_GL_CreateContext(win);
        SDL_GL_SetSwapInterval(1);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_PROGRAM_POINT_SIZE);

        IMGUI_CHECKVERSION(); ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding=4; ImGui::GetStyle().FrameRounding=2;
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg]=ImVec4(0.06f,0.06f,0.09f,0.95f);
        ImGui_ImplSDL2_InitForOpenGL(win,gl);
        ImGui_ImplOpenGL3_Init("#version 330");

        pProg = mkProg(pVert, pFrag);
        parts.init(); lines.init(); grid.init();
    }

    void desel() { for(int i:sel) if(i<(int)bodies.size()) bodies[i].selected=false; sel.clear(); }
    void selBody(int i) { bodies[i].selected=true; if(std::find(sel.begin(),sel.end(),i)==sel.end()) sel.push_back(i); }

    void group() {
        if(sel.size()<2)return;
        Body m; m.mass=0; m.crossSection=0;
        for(int idx:sel) {
            Body& b=bodies[idx]; float ca=cosf(b.angle),sa=sinf(b.angle);
            for(auto& s:b.strokes) {
                Stroke ns=s;
                ns.offset={b.center.x+s.offset.x*ca-s.offset.y*sa, b.center.y+s.offset.x*sa+s.offset.y*ca};
                for(auto& p:ns.points) p=p.rotate(b.angle);
                m.strokes.push_back(ns);
            }
            m.mass+=b.mass; m.crossSection+=b.crossSection;
        }
        std::sort(sel.begin(),sel.end(),std::greater<int>());
        for(int i:sel) bodies.erase(bodies.begin()+i);
        sel.clear();
        m.recomputeCenter(); m.dragCoef=0.75f; m.saveOrigin(); m.selected=true;
        bodies.push_back(m); sel.push_back((int)bodies.size()-1);
    }

    void undo() {
        if(bodies.empty())return; bodies.pop_back();
        sel.erase(std::remove_if(sel.begin(),sel.end(),[&](int i){return i>=(int)bodies.size();}),sel.end());
    }

    void delSel() {
        std::sort(sel.begin(),sel.end(),std::greater<int>());
        for(int i:sel) bodies.erase(bodies.begin()+i);
        sel.clear();
    }

    void event(SDL_Event& e) {
        ImGuiIO& io=ImGui::GetIO();
        if(io.WantCaptureMouse||io.WantCaptureKeyboard)return;
        float s=dpi();

        if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
            float fx=e.button.x*s, fy=e.button.y*s;
            if(tool==0) {
                isDrawing=true; drawOrig={fx,fy};
                curStroke=Stroke{}; curStroke.offset={0,0};
                curStroke.points.push_back({0,0});
            } else if(tool==1) {
                for(int i:sel) if(bodies[i].hitTest({fx,fy})){isDrag=true;dragSt={fx,fy};return;}
                desel();
                for(int i=(int)bodies.size()-1;i>=0;i--) if(bodies[i].hitTest({fx,fy})){selBody(i);isDrag=true;dragSt={fx,fy};return;}
                isBox=true; boxA=boxB={fx,fy};
            } else if(tool==2) {
                Body b; float h=90;
                float headR=8, headY=-h*0.42f;
                float neckY=headY+headR, shoulderY=neckY+h*0.08f;
                float hipY=shoulderY+h*0.30f;
                float armLen=h*0.22f, legLen=h*0.32f;
                auto mkS=[](std::vector<Vec2> pts){Stroke s; s.offset={0,0}; s.points=pts; s.computeBounds(); return s;};
                // Head: smooth circle with 32 points
                std::vector<Vec2> headPts;
                for(int i=0;i<=32;i++){float a=(float)i/32*2*M_PI; headPts.push_back({cosf(a)*headR, headY+sinf(a)*headR});}
                // Torso
                std::vector<Vec2> torso={{0,neckY},{0,hipY}};
                // Arms (slightly angled down)
                std::vector<Vec2> armL={{0,shoulderY},{-armLen,shoulderY+h*0.05f}};
                std::vector<Vec2> armR={{0,shoulderY},{armLen,shoulderY+h*0.05f}};
                // Legs
                std::vector<Vec2> legL={{0,hipY},{-h*0.12f,hipY+legLen}};
                std::vector<Vec2> legR={{0,hipY},{h*0.12f,hipY+legLen}};
                b.strokes={mkS(headPts), mkS(torso), mkS(armL), mkS(armR), mkS(legL), mkS(legR)};
                b.center={fx,fy}; b.computeRadius(); b.mass=80; b.dragCoef=0.8f; b.crossSection=0.7f; b.saveOrigin();
                bodies.push_back(b);
            }
        }
        if(e.type==SDL_MOUSEMOTION) {
            float fx=e.motion.x*s, fy=e.motion.y*s;
            if(isDrawing) {
                Vec2 rel={fx-drawOrig.x,fy-drawOrig.y};
                auto& pts=curStroke.points; Vec2 last=pts.back();
                if((rel-last).mag2()>16) pts.push_back(rel);
            }
            if(isDrag&&!sel.empty()) {
                Vec2 d={fx-dragSt.x,fy-dragSt.y};
                for(int i:sel) bodies[i].center+=d;
                dragSt={fx,fy};
            }
            if(isBox) boxB={fx,fy};
        }
        if(e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_LEFT) {
            if(isDrawing && curStroke.points.size()>=3) {
                curStroke.computeBounds();
                Body b; b.center=drawOrig+curStroke.offset;
                curStroke.offset={0,0};
                b.strokes.push_back(curStroke); b.radius=curStroke.radius;
                b.mass=5; b.crossSection=fmaxf(b.radius/100,0.1f); b.dragCoef=0.5f; b.saveOrigin();
                bodies.push_back(b);
            }
            isDrawing=false;
            if(isBox) {
                float x1=fminf(boxA.x,boxB.x),y1=fminf(boxA.y,boxB.y),x2=fmaxf(boxA.x,boxB.x),y2=fmaxf(boxA.y,boxB.y);
                if(x2-x1>5||y2-y1>5){desel(); for(int i=0;i<(int)bodies.size();i++){Vec2 c=bodies[i].center; if(c.x>=x1&&c.x<=x2&&c.y>=y1&&c.y<=y2)selBody(i);}}
                isBox=false;
            }
            isDrag=false;
        }
        if(e.type==SDL_KEYDOWN) {
            if(e.key.keysym.sym==SDLK_z&&(e.key.keysym.mod&KMOD_GUI)) undo();
            if(e.key.keysym.sym==SDLK_DELETE||e.key.keysym.sym==SDLK_BACKSPACE) delSel();
        }
    }

    void update(float dt) {
        int dw,dh; SDL_GL_GetDrawableSize(win,&dw,&dh); sw=dw; sh=dh;
        grid.rebuild(sw,sh);
        if(simOn) { int steps=std::max(1,(int)ceilf(dt/0.005f)); float sd=dt/steps;
            for(int s=0;s<steps;s++) for(auto& b:bodies) physicsStep(b,sd,wVel()); }
        for(auto& b:bodies) b.computeRadius();
        buildWorldSegs(bodies);
        Vec2 wp=wPx();
        parts.update(dt, wp.x, wp.y, windSpd, sw, sh);
    }

    void render() {
        glViewport(0,0,sw,sh);
        glClearColor(0.05f,0.05f,0.08f,1); glClear(GL_COLOR_BUFFER_BIT);
        grid.render(lines.prog, sw, sh);

        glUseProgram(pProg);
        glUniform2f(glGetUniformLocation(pProg,"uS"),(float)sw,(float)sh);
        parts.render();

        // Batch all body strokes: one draw call for normal, one for selected
        lines.begin();
        for(auto& body:bodies) {
            if(body.selected) continue;
            float ca=cosf(body.angle),sa=sinf(body.angle);
            for(auto& st:body.strokes) {
                Vec2 wp=body.center+Vec2{st.offset.x*ca-st.offset.y*sa, st.offset.x*sa+st.offset.y*ca};
                lines.addStrip(st.points, wp, body.angle, 3.0f);
            }
        }
        lines.draw(0.88f,0.88f,0.94f,1, sw,sh);

        lines.begin();
        for(auto& body:bodies) {
            if(!body.selected) continue;
            float ca=cosf(body.angle),sa=sinf(body.angle);
            for(auto& st:body.strokes) {
                Vec2 wp=body.center+Vec2{st.offset.x*ca-st.offset.y*sa, st.offset.x*sa+st.offset.y*ca};
                lines.addStrip(st.points, wp, body.angle, 4.0f);
            }
        }
        lines.draw(1,0.67f,0.27f,1, sw,sh);

        // Drawing preview
        if(isDrawing && curStroke.points.size()>=2) {
            lines.begin();
            lines.addStrip(curStroke.points, drawOrig, 0, 3.0f);
            lines.draw(0.88f,0.88f,0.94f,0.7f, sw,sh);
        }
        // Box select
        if(isBox) {
            float x1=fminf(boxA.x,boxB.x),y1=fminf(boxA.y,boxB.y),x2=fmaxf(boxA.x,boxB.x),y2=fmaxf(boxA.y,boxB.y);
            std::vector<Vec2> bx={{x1,y1},{x2,y1},{x2,y2},{x1,y2},{x1,y1}};
            lines.begin(); lines.addStrip(bx,{0,0},0,1); lines.draw(0.4f,0.6f,1,0.5f,sw,sh);
        }

        // UI
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10,10),ImGuiCond_FirstUseEver);
        ImGui::Begin("Tools",nullptr,ImGuiWindowFlags_AlwaysAutoResize);
        if(ImGui::RadioButton("Draw",tool==0)){tool=0;desel();} ImGui::SameLine();
        if(ImGui::RadioButton("Select",tool==1))tool=1; ImGui::SameLine();
        if(ImGui::RadioButton("Stick",tool==2))tool=2;
        ImGui::Separator();
        if(ImGui::Button("Undo"))undo(); ImGui::SameLine(); if(ImGui::Button("Delete"))delSel();
        if(sel.size()>=2){ImGui::SameLine(); if(ImGui::Button("Group"))group();}
        ImGui::Separator();
        if(ImGui::Button(simOn?"Pause":"Play")){simOn=!simOn; if(simOn)for(auto&b:bodies)b.saveOrigin();}
        ImGui::SameLine(); if(ImGui::Button("Reset")){simOn=false; for(auto&b:bodies)b.resetToOrigin();}
        ImGui::Separator();
        ImGui::SliderFloat("Wind m/s",&windSpd,0,80);
        ImGui::SliderFloat("Direction",&windAng,0,359);
        if(!sel.empty()){ImGui::Separator();
            if(ImGui::Button("-5")){for(int i:sel)bodies[i].angle-=5*M_PI/180;} ImGui::SameLine();
            if(ImGui::Button("+5")){for(int i:sel)bodies[i].angle+=5*M_PI/180;}
        }
        ImGui::Text("Particles: %d | Bodies: %d", parts.count, (int)bodies.size());
        ImGui::End();

        if(sel.size()==1) {
            Body& b=bodies[sel[0]]; float px=(float)sw/dpi()-230;
            ImGui::SetNextWindowPos(ImVec2(px,10),ImGuiCond_FirstUseEver);
            ImGui::Begin("Metadata",nullptr,ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Angle: %.1f deg", fmodf(b.angle*180/M_PI,360.0f));
            ImGui::Text("Alt: %.0fm (%.0fft)", b.altitude, b.altitude*3.281f);
            ImGui::Text("Air: %.3f kg/m3", b.airDensity);
            ImGui::Separator();
            ImGui::Text("%.1f m/s (%.0f mph)", b.speedMps, b.speedMps*2.237f);
            ImGui::Text("Terminal: %.0f mph", b.terminalV*2.237f);
            ImGui::Text("AoA: %.1f deg", b.relWindAngle);
            ImGui::Separator();
            ImGui::SliderFloat("Mass",&b.mass,1,150);
            ImGui::SliderFloat("Cd",&b.dragCoef,0.1f,1.5f);
            ImGui::SliderFloat("Area",&b.crossSection,0.1f,2.0f);
            ImGui::Checkbox("Pinned",&b.pinned);
            ImGui::End();
        } else if(sel.size()>1) {
            ImGui::Begin("Group",nullptr,ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("%d selected",(int)sel.size());
            if(ImGui::Button("Group into Body"))group();
            ImGui::End();
        }
        ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }

    void run() {
        init(); bool on=true;
        Uint64 lt=SDL_GetPerformanceCounter(), freq=SDL_GetPerformanceFrequency();
        while(on) {
            SDL_Event e;
            while(SDL_PollEvent(&e)){ImGui_ImplSDL2_ProcessEvent(&e); if(e.type==SDL_QUIT)on=false; event(e);}
            Uint64 now=SDL_GetPerformanceCounter(); float dt=fminf((float)(now-lt)/freq,0.05f); lt=now;
            update(dt); render();
        }
        parts.cleanup(); lines.cleanup(); grid.cleanup(); glDeleteProgram(pProg);
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
        SDL_GL_DeleteContext(gl); SDL_DestroyWindow(win); SDL_Quit();
    }
};

int main(int,char**){App a; a.run(); return 0;}
