// Folio Gable — geometry engine: sticky edges, automatic face finding, push/pull, etc.
#pragma once
#include <string>
#include "core/Model.h"
#include <set>
#include <unordered_set>

namespace gable {
namespace geom {

// Snapshot of a face that survives its removal (used to inherit materials/orientation).
struct FaceSnap {
    std::vector<std::vector<Vec3>> loops;  // [0] outer, rest holes
    Plane plane;
    int matFront = -1, matBack = -1;
    TexMap texFront, texBack;
    bool hidden = false;
    int tag = 0;
};
FaceSnap snapFace(const Entities& E, int f);

// Inserts the segment a-b, splitting existing edges where they touch or cross.
// Returns the edges making up the segment; created edges are appended to createdOut.
std::vector<int> insertSegment(Entities& E, const Vec3& a, const Vec3& b, std::vector<int>* createdOut = nullptr);

// Draws a line and creates any faces it closes. Returns created edges.
std::vector<int> addLine(Entities& E, const Vec3& a, const Vec3& b);

// Draws a polyline/loop (optionally as one curve) and creates faces.
std::vector<int> addPolyline(Entities& E, const std::vector<Vec3>& pts, bool closed, const Curve* curve = nullptr);

// Splits edge e at point p. Returns the vertex at p.
int splitEdge(Entities& E, int e, const Vec3& p);

// Finds faces closed by the given (new) edges.
void findFaces(Entities& E, const std::vector<int>& newEdges);

enum class RebuildMode { Draw, MergeOnly };
// Recomputes faces lying in plane P from the planar edge arrangement.
std::vector<int> rebuildPlane(Entities& E, const Plane& P, const std::unordered_set<int>& newEdges,
                              const std::vector<FaceSnap>& removed, RebuildMode mode);

// Erase edges (faces bounded by them disappear, coplanar neighbours merge).
void eraseEdges(Entities& E, const std::vector<int>& edges);
void eraseFaces(Entities& E, const std::vector<int>& faces);
// Removes an edge between two coplanar faces, merging them. Returns true if merged.
bool mergeCoplanarAcross(Entities& E, const std::vector<int>& edges);

// Push/Pull face f by dist along its normal. keepOriginal = Ctrl (new starting face).
// Returns the resulting face that sits at the pushed position (or -1).
int pushPull(Entities& E, int f, double dist, bool keepOriginal, std::vector<int>* createdFaces = nullptr);

void reverseFace(Entities& E, int f);
bool offsetFace(Entities& E, int f, double dist);
std::vector<std::vector<Vec3>> offsetFaceLoops(const Entities& E, int f, double dist);
bool offsetEdges(Entities& E, const std::vector<int>& edges, double dist, const Vec3& planeNormal);

// Sweeps face f along a path. The path must start on the face's plane (near it).
bool followMe(Entities& E, int f, std::vector<Vec3> path, bool closedPath);
// Lathe: sweep face f around axis.
bool followMeAround(Entities& E, int f, const Vec3& center, const Vec3& axis, int segments);

// Intersect faces: adds edges where faces of set A cut faces of set B (B empty = all faces).
int intersectFaces(Entities& E, const std::vector<int>& facesA, const std::vector<int>& facesB);
// Segments where face fa of A crosses face fb of B (both in the same coordinate space).
std::vector<std::pair<Vec3, Vec3>> faceFaceSegments(const Entities& A, int fa, const Entities& B, int fb);
// Splits edges that cross each other (among the given edges and all others).
void intersectEdges(Entities& E, std::vector<int> edges);

// Moves vertices; heals: merges coincident vertices, re-planarizes or triangulates warped faces.
void transformVertices(Entities& E, const std::vector<int>& verts, const Mat4& m, bool heal = true);
void mergeVertices(Entities& E, int keep, int dup);
void fixWarpedFaces(Entities& E, const std::vector<int>& faces);

// Triangulate a face (for rendering / export). Returns triangle corner positions.
void faceTriangles(const Entities& E, int f, std::vector<Vec3>& out);

// Copy geometry/instances from src into dst with a transform. Returns new refs.
struct CopyMap {
    std::unordered_map<int, int> verts, edges, faces, insts, guides, dims, texts, curves;
};
std::vector<EntRef> copyEntities(const Entities& src, const std::vector<EntRef>& sel, Entities& dst,
                                 const Mat4& xf, bool sticky, CopyMap* mapOut = nullptr);

// Expands a selection with all geometry needed to stand alone (faces -> their edges).
std::vector<EntRef> closeSelection(const Entities& E, const std::vector<EntRef>& sel);
std::vector<int> selectionVertices(const Entities& E, const std::vector<EntRef>& sel);

// Connected geometry helpers
std::vector<EntRef> connected(const Entities& E, EntRef start, bool all);
std::vector<int> curveEdges(const Entities& E, int curve);

// Volume of a closed manifold (0 if not solid).
bool isSolid(const Entities& E);
double volume(const Entities& E);

// Faces bbox overlap
BBox faceBBox(const Entities& E, int f);

Vec3 newellNormal(const Entities& E, const std::vector<int>& loop);
// Topology/geometry integrity check; returns human-readable problems (empty = healthy).
std::vector<std::string> validate(const Entities& E, int maxReports = 20);

}  // namespace geom
}  // namespace gable
