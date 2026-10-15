#pragma once
#include "core/Model.h"

namespace gable {

struct ImportOptions {
    double scale = 1.0;           // multiply incoming coordinates (to mm)
    bool mergeCoplanar = true;    // remove edges between coplanar triangles
    double smoothAngle = 20;      // soften edges with dihedral angle below this (degrees)
    bool swapYZ = false;          // Y-up source
    bool asGroup = true;
};

struct ExportOptions {
    double scale = 1.0;           // divide mm by this (e.g. 1000 for metres)
    bool swapYZ = false;
    bool exportEdges = false;
    bool triangulate = true;
    bool binary = true;           // STL
    bool selectionOnly = false;
};

// Imports into a new group (definition) inside target; returns instance index or -1.
int importFile(Model& m, Entities& target, const QString& path, const ImportOptions& opt, QString* err);
bool exportFile(const Model& m, const QString& path, const ExportOptions& opt, QString* err,
                const std::vector<EntRef>* sel = nullptr);

// Flattened world geometry for export & rendering helpers.
struct WorldTri { Vec3 p[3]; Vec3 n; int material; };
struct WorldEdge { Vec3 a, b; };
void collectWorld(const Model& m, std::vector<WorldTri>& tris, std::vector<WorldEdge>* edges, bool visibleOnly,
                  const std::vector<EntRef>* sel = nullptr);

// Post-processing for meshes built from triangles/polygons.
void cleanupImportedMesh(Entities& E, const ImportOptions& opt);

}  // namespace gable
