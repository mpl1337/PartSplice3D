#pragma once

#include "stl_io.h"
#include "connector_types.h"

#include <manifold/manifold.h>

#include <string>
#include <vector>

class MeshSpatialIndex;

struct ConnectorPreview {
    Vec2 center;
    Vec2 tangent;
    Vec2 left;
    std::vector<Vec2> maleOutline;
    std::vector<Vec2> socketOutline;
    bool maleOnLeft = true;
    bool valid = true;
    // Set only by the application when a cut affects several source parts:
    // the connector is valid in at least one, but not in every target part.
    bool partiallyValid = false;
    bool overlap = false;
    // The complete male/socket tool crosses another segment of the current
    // polyline. Applying it at a bend can remove material from the wrong half
    // and leave a cavity, even when both halves contain sufficient material.
    bool crossesCutCorner = false;
    bool thinWall = false;
    bool intersectsExistingCut = false;

    bool warning() const { return thinWall || intersectsExistingCut; }
};

struct DovetailResult {
    bool ok = false;
    TriangleMesh partA;
    TriangleMesh partB;
    std::vector<ConnectorPreview> connectors;
    std::string message;
};

struct AssemblyMarkPreview {
    bool valid = false;
    int code = 1;
    Vec2 leftCenter{};
    Vec2 rightCenter{};
    std::vector<std::vector<Vec2>> outlines;
};

enum class MeshIssueKind { OpenEdge, NonManifoldEdge, OrientationConflict };

struct MeshIssueEdge {
    Vec3 a;
    Vec3 b;
    MeshIssueKind kind = MeshIssueKind::OpenEdge;
};

struct MeshDiagnostics {
    size_t degenerateTriangles = 0;
    size_t openEdges = 0;
    size_t nonManifoldEdges = 0;
    size_t orientationConflicts = 0;
    std::vector<MeshIssueEdge> edges;

    bool validSolid() const {
        return degenerateTriangles == 0 && openEdges == 0 &&
               nonManifoldEdges == 0 && orientationConflicts == 0;
    }

    size_t problemEdgeCount() const {
        return openEdges + nonManifoldEdges + orientationConflicts;
    }
};

struct ConnectorCalibrationSample {
    double clearance = 0.0;
    TriangleMesh male;
    TriangleMesh socket;
};

manifold::Manifold meshToManifold(const TriangleMesh& mesh);
TriangleMesh manifoldToMesh(const manifold::Manifold& solid);
MeshDiagnostics analyzeMesh(const TriangleMesh& mesh);

std::vector<ConnectorCalibrationSample> createConnectorCalibrationSamples(
    const DovetailSettings& settings, double startClearance, double step, int count,
    std::string& error);

bool arrangeConnectorCalibrationSamples(std::vector<ConnectorCalibrationSample>& samples,
                                        double bedWidth, double bedDepth,
                                        double margin, double gap,
                                        std::string& error);

std::vector<ConnectorPlacement> makeDefaultConnectorPlacements(const std::vector<Vec2>& cutPoints,
                                                                const DovetailSettings& settings);

std::vector<ConnectorPreview> makeConnectorPreview(const TriangleMesh& source,
                                                    const std::vector<Vec2>& cutPoints,
                                                    const DovetailSettings& settings,
                                                    const std::vector<ConnectorPlacement>& placements,
                                                    const MeshSpatialIndex* preparedValidationIndex = nullptr,
                                                    bool exactMaterialValidation = true);

AssemblyMarkPreview makeAssemblyMarkPreview(const std::vector<Vec2>& cutPoints,
                                            const DovetailSettings& settings,
                                            int code);

DovetailResult createDovetailSplit(const TriangleMesh& source,
                                   const std::vector<Vec2>& cutPoints,
                                   const DovetailSettings& settings,
                                   const std::vector<ConnectorPlacement>& placements,
                                   bool requireConnector = true);

std::vector<TriangleMesh> decomposeMesh(const TriangleMesh& mesh);

double polylineLength(const std::vector<Vec2>& cutPoints);
double closestPositionOnPolyline(const std::vector<Vec2>& cutPoints, Vec2 point);
bool validateCutPolyline(const std::vector<Vec2>& cutPoints, std::string& message);

std::string manifoldErrorText(manifold::Manifold::Error error);
