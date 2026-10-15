#pragma once
#include "core/Model.h"
#include <QByteArray>

namespace gable {

// Native .gbl format (compressed binary). Also used for undo snapshots (uncompressed).
QByteArray saveModel(const Model& m, bool compress = true);
bool loadModel(Model& m, const QByteArray& data, QString* error = nullptr);

// Removes dead entries from an Entities, remapping all indices.
void compactEntities(Entities& e);

// Merges src into dst: materials are matched by name, definitions appended.
// src.root contents are copied into dstEnts with transform xf. Returns refs of new top-level entities.
std::vector<EntRef> mergeModelInto(Model& dst, Entities& dstEnts, const Model& src, const Mat4& xf);

}  // namespace gable
