#include "core/Tessellate.h"
#include <GL/glu.h>
#include <deque>
#include <array>

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
    TessCtx ctx;
    ctx.out = &out;
    GLUtesselator* t = gluNewTess();
    gluTessCallback(t, GLU_TESS_BEGIN_DATA, (void(CALLBACK*)())cbBegin);
    gluTessCallback(t, GLU_TESS_EDGE_FLAG_DATA, (void(CALLBACK*)())cbEdgeFlag);
    gluTessCallback(t, GLU_TESS_VERTEX_DATA, (void(CALLBACK*)())cbVertex);
    gluTessCallback(t, GLU_TESS_END_DATA, (void(CALLBACK*)())cbEnd);
    gluTessCallback(t, GLU_TESS_ERROR_DATA, (void(CALLBACK*)())cbError);
    gluTessCallback(t, GLU_TESS_COMBINE_DATA, (void(CALLBACK*)())cbCombine);
    gluTessProperty(t, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
    gluTessNormal(t, 0, 0, 1);
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
    gluDeleteTess(t);
    // GLU with normal +Z emits CCW triangles.
    return !ctx.error && !out.tris.empty();
}

}  // namespace gable
