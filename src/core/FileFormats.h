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

// Exports also cover glTF 2.0 (.gltf with the data inside, or binary .glb), Y-up in metres as
// the format requires, and 3MF for printing (one object per closed shell, millimetres).

// Imports into a new group (definition) inside target; returns instance index or -1.
// Formats: Folio Gable (.gbl), OBJ, STL, COLLADA (.dae), DXF, 3D Studio (.3ds), Google Earth (.kmz)
// and IFC (.ifc, building parts as groups on a tag per IFC class).
int importFile(Model& m, Entities& target, const QString& path, const ImportOptions& opt, QString* err);
// What the last import couldn't bring in fully, for the user (e.g. IFC openings); cleared when read.
QStringList takeImportNotes();

bool exportFile(const Model& m, const QString& path, const ExportOptions& opt, QString* err,
                const std::vector<EntRef>* sel = nullptr);

// Flattened world geometry for export & rendering helpers.
// uv: texture coordinates in tiles of the material's texture (v up), from the face's mapping
struct WorldTri { Vec3 p[3]; Vec3 n; int material; Vec2 uv[3]; };
struct WorldEdge { Vec3 a, b; };
void collectWorld(const Model& m, std::vector<WorldTri>& tris, std::vector<WorldEdge>* edges, bool visibleOnly,
                  const std::vector<EntRef>* sel = nullptr);

// Post-processing for meshes built from triangles/polygons.
void cleanupImportedMesh(Entities& E, const ImportOptions& opt);

}  // namespace gable
