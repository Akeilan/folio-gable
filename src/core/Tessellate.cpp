#include "core/Tessellate.h"
#include <GL/glu.h>
#include <deque>
#include <array>
#include <algorithm>
#include <cmath>

#ifndef CALLBACK
#define CALLBACK
#endif

namespace gable {

namespace {
struct TessCtx {
    TessResult* out;
    std::deque<std::array<double, 3>> coords;   // stable storage for GLU
    std::deque<int> ids;
    bool error = false;
    int cur[3];
    int n = 0;
};

void CALLBACK cbBegin(GLenum, void*) {}
void CALLBACK cbEdgeFlag(GLboolean, void*) {}
void CALLBACK cbVertex(void* vd, void* ud) {
    auto* c = static_cast<TessCtx*>(ud);
    c->cur[c->n++] = *static_cast<int*>(vd);
    if (c->n == 3) {
        c->out->tris.push_back(c->cur[0]);
        c->out->tris.push_back(c->cur[1]);
        c->out->tris.push_back(c->cur[2]);
        c->n = 0;
    }
}
void CALLBACK cbEnd(void* ud) { static_cast<TessCtx*>(ud)->n = 0; }
void CALLBACK cbError(GLenum, void* ud) { static_cast<TessCtx*>(ud)->error = true; }
void CALLBACK cbCombine(GLdouble coords[3], void*[4], GLfloat[4], void** outData, void* ud) {
    auto* c = static_cast<TessCtx*>(ud);
    c->out->pts.push_back(Vec2(coords[0], coords[1]));
    c->ids.push_back((int)c->out->pts.size() - 1);
    *outData = &c->ids.back();
}
}  // namespace

bool tessellate(const std::vector<std::vector<Vec2>>& loops, TessResult& out) {
    out.pts.clear();
    out.tris.clear();
    for (auto& l : loops) out.pts.insert(out.pts.end(), l.begin(), l.end());
    if (out.pts.size() < 3) return false;
    // Fast path: single convex-ish triangle or quad.
    if (loops.size() == 1 && loops[0].size() == 3) {
        out.tris = {0, 1, 2};
        if (signedArea2(out.pts.data(), 3) < 0) std::swap(out.tris[1], out.tris[2]);
        return true;
    }
    // Fast path: a single strictly convex loop is a fan (most faces of real models).
    if (loops.size() == 1 && loops[0].size() <= 64) {
        const std::vector<Vec2>& L = loops[0];
        const int n = (int)L.size();
        double area = signedArea2(L.data(), n);
        if (std::fabs(area) > 1e-12) {
            const double sgn = area > 0 ? 1.0 : -1.0;
            bool convex = true;
            for (int i = 0; i < n && convex; ++i) {
                const Vec2& a = L[i];
                const Vec2& b = L[(i + 1) % n];
                const Vec2& c = L[(i + 2) % n];
                double cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
                // strictly convex corners only; collinear or reflex corners go to the general tessellator
                double scale = std::max(1e-300, (b - a).length() * (c - b).length());
                if (cr * sgn <= 1e-9 * scale) convex = false;
            }
            if (convex) {
                out.tris.reserve((n - 2) * 3);
                for (int i = 1; i + 1 < n; ++i) {
                    out.tris.push_back(0);
                    if (sgn > 0) { out.tris.push_back(i); out.tris.push_back(i + 1); }
                    else { out.tris.push_back(i + 1); out.tris.push_back(i); }
                }
                return true;
            }
        }
    }
    TessCtx ctx;
    ctx.out = &out;
    // One tessellator per thread, set up once (creating one per face is expensive).
    thread_local GLUtesselator* t = nullptr;
    if (!t) {
        t = gluNewTess();
        gluTessCallback(t, GLU_TESS_BEGIN_DATA, (void(CALLBACK*)())cbBegin);
        gluTessCallback(t, GLU_TESS_EDGE_FLAG_DATA, (void(CALLBACK*)())cbEdgeFlag);
        gluTessCallback(t, GLU_TESS_VERTEX_DATA, (void(CALLBACK*)())cbVertex);
        gluTessCallback(t, GLU_TESS_END_DATA, (void(CALLBACK*)())cbEnd);
        gluTessCallback(t, GLU_TESS_ERROR_DATA, (void(CALLBACK*)())cbError);
        gluTessCallback(t, GLU_TESS_COMBINE_DATA, (void(CALLBACK*)())cbCombine);
        gluTessProperty(t, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
        gluTessNormal(t, 0, 0, 1);
    }
    gluTessBeginPolygon(t, &ctx);
    int base = 0;
    for (auto& l : loops) {
        gluTessBeginContour(t);
        for (size_t i = 0; i < l.size(); ++i) {
            ctx.coords.push_back({l[i].x, l[i].y, 0});
            ctx.ids.push_back(base + (int)i);
            gluTessVertex(t, ctx.coords.back().data(), &ctx.ids.back());
        }
        gluTessEndContour(t);
        base += (int)l.size();
    }
    gluTessEndPolygon(t);
    // GLU with normal +Z emits CCW triangles.
    return !ctx.error && !out.tris.empty();
}

}  // namespace gable
