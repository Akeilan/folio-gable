#pragma once
#include "core/Model.h"

namespace gable {

enum class SolidOp { Union, Subtract, Intersect, Trim, Split, OuterShell };

// Performs a solid operation on two instances in ctx. "first" is the tool picked first
// (the cutter for Subtract/Trim). Results are appended as new group instances.
// Returns false with an error message when either is not a solid.
bool solidOperation(Model& m, Entities& ctx, int first, int second, SolidOp op, QString* err,
                    std::vector<int>* resultInsts = nullptr);

// Flattens an instance's geometry (with nested instances exploded) into world-space entities.
void flattenInstance(const Model& m, const Instance& in, const Mat4& parentXf, Entities& out);

bool instanceIsSolid(const Model& m, const Instance& in);
double instanceVolume(const Model& m, const Instance& in);

}  // namespace gable
