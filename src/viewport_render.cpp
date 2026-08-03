#include "viewport_render.h"

#include "app_queries.h"
#include "model_projection.h"
#include "visual_style.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

void setupLighting() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    const GLfloat pos[] = {0.25f, 0.5f, 1.0f, 0.0f};
    const GLfloat ambient[] = {0.25f, 0.25f, 0.25f, 1.0f};
    const GLfloat diffuse[] = {0.85f, 0.85f, 0.85f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
}

void drawGrid(const AABB& box) {
    if (!box.valid()) return;
    const Vec3 s = box.size();
    const double extent = std::max({s.x, s.y, 20.0}) * 0.8;
    const Vec3 c = box.center();
    double step = 10.0;
    if (extent > 500.0) step = 50.0;
    else if (extent > 200.0) step = 20.0;
    else if (extent < 50.0) step = 5.0;
    glDisable(GL_LIGHTING);
    glColor3f(0.28f, 0.30f, 0.33f);
    glBegin(GL_LINES);
    const double z = box.min.z - std::max(0.05, s.z * 0.01);
    const double minX = std::floor((c.x - extent) / step) * step;
    const double maxX = std::ceil((c.x + extent) / step) * step;
    const double minY = std::floor((c.y - extent) / step) * step;
    const double maxY = std::ceil((c.y + extent) / step) * step;
    for (double x = minX; x <= maxX; x += step) {
        glVertex3d(x, minY, z); glVertex3d(x, maxY, z);
    }
    for (double y = minY; y <= maxY; y += step) {
        glVertex3d(minX, y, z); glVertex3d(maxX, y, z);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void drawOutline(const std::vector<Vec2>& outline, double z, bool valid,
                 bool partiallyValid, bool thinWall, bool existingCutConflict,
                 bool socket) {
    glDisable(GL_LIGHTING);
    if (partiallyValid) glColor3f(1.00f, 0.56f, 0.08f);
    else if (!valid) glColor3f(0.95f, 0.25f, 0.20f);
    else if (existingCutConflict) glColor3f(0.88f, 0.30f, 1.00f);
    else if (thinWall) glColor3f(1.00f, 0.86f, 0.12f);
    else if (socket) glColor3f(1.0f, 0.70f, 0.10f);
    else glColor3f(0.15f, 0.95f, 0.35f);
    glLineWidth(socket ? 1.0f : 2.0f);
    glBegin(GL_LINE_LOOP);
    for (const auto& p : outline) glVertex3d(p.x, p.y, z);
    glEnd();
    glEnable(GL_LIGHTING);
}

void drawSquare(Vec2 center, double halfSize, double z, float r, float g, float b) {
    glDisable(GL_LIGHTING);
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex3d(center.x - halfSize, center.y - halfSize, z);
    glVertex3d(center.x + halfSize, center.y - halfSize, z);
    glVertex3d(center.x + halfSize, center.y + halfSize, z);
    glVertex3d(center.x - halfSize, center.y + halfSize, z);
    glEnd();
    glEnable(GL_LIGHTING);
}

void drawDirectionArrow(const ConnectorPreview& connector, double z, double scale) {
    const Vec2 ownerNormal = connector.maleOnLeft ? connector.left : connector.left * -1.0;
    const Vec2 direction = ownerNormal * -1.0;
    const Vec2 tip = connector.center + direction * scale;
    const Vec2 side = leftNormal(direction);
    const Vec2 wingA = tip - direction * (scale * 0.32) + side * (scale * 0.22);
    const Vec2 wingB = tip - direction * (scale * 0.32) - side * (scale * 0.22);
    glDisable(GL_LIGHTING);
    glColor3f(0.20f, 0.85f, 1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex3d(connector.center.x, connector.center.y, z);
    glVertex3d(tip.x, tip.y, z);
    glVertex3d(tip.x, tip.y, z);
    glVertex3d(wingA.x, wingA.y, z);
    glVertex3d(tip.x, tip.y, z);
    glVertex3d(wingB.x, wingB.y, z);
    glEnd();
    glEnable(GL_LIGHTING);
}

Vec2 rotate2(Vec2 p, double degrees) {
    const double angle = degrees * std::numbers::pi / 180.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}

void drawPrintBed(const App& app, double z, double handleHalf) {
    if (!app.showPrintBed) return;
    const double halfW = app.printBedWidth * 0.5;
    const double halfD = app.printBedDepth * 0.5;
    const Vec2 local[] = {{-halfW, -halfD}, {halfW, -halfD}, {halfW, halfD}, {-halfW, halfD}};
    glDisable(GL_LIGHTING);
    glColor3f(0.82f, 0.25f, 0.95f);
    glLineWidth(2.5f);
    glBegin(GL_LINE_LOOP);
    for (Vec2 p : local) {
        const Vec2 world = app.printBedCenter + rotate2(p, app.printBedRotation);
        glVertex3d(world.x, world.y, z);
    }
    glEnd();
    glEnable(GL_LIGHTING);
    drawSquare(app.printBedCenter, handleHalf * 0.85, z + 0.01, 0.82f, 0.25f, 0.95f);
}

void drawMeshIssueEdges(const MeshDiagnostics& diagnostics) {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(4.0f);
    for (MeshIssueKind kind : {MeshIssueKind::OpenEdge,
                               MeshIssueKind::NonManifoldEdge,
                               MeshIssueKind::OrientationConflict}) {
        if (kind == MeshIssueKind::OpenEdge) glColor3f(1.0f, 0.12f, 0.08f);
        else if (kind == MeshIssueKind::NonManifoldEdge) glColor3f(1.0f, 0.05f, 0.82f);
        else glColor3f(1.0f, 0.78f, 0.08f);
        glBegin(GL_LINES);
        for (const MeshIssueEdge& edge : diagnostics.edges) {
            if (edge.kind != kind) continue;
            glVertex3d(edge.a.x, edge.a.y, edge.a.z);
            glVertex3d(edge.b.x, edge.b.y, edge.b.z);
        }
        glEnd();
    }
    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

Vec2 partExplosionOffset(const App& app, const PartRecord& part, Vec3 modelCenter) {
    if (!part.mesh || app.explode <= 1e-9) return {};
    Vec2 direction{part.mesh->bounds.center().x - modelCenter.x,
                   part.mesh->bounds.center().y - modelCenter.y};
    if (length(direction) < 1e-9) direction = {1.0, 0.0};
    return normalized(direction) * app.explode;
}

std::optional<Vec2> explosionOffsetAt(const App& app, Vec2 modelPoint, Vec3 modelCenter) {
    for (const PartRecord& part : app.parts) {
        if (!part.active || !part.mesh) continue;
        const bool inside = part.spatialIndex
            ? pointInsideMeshProjection(*part.spatialIndex, modelPoint)
            : pointInsideMeshProjection(*part.mesh, modelPoint);
        if (inside) return partExplosionOffset(app, part, modelCenter);
    }
    return std::nullopt;
}

void drawAssemblyMarkOverlay(const App& app, const AssemblyMarkPreview& mark,
                             Vec3 modelCenter, double z, bool followExplodedParts) {
    if (!app.camera.top || !mark.valid) return;
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.5f);
    for (const std::vector<Vec2>& outline : mark.outlines) {
        if (outline.empty()) continue;
        Vec2 center{};
        for (Vec2 point : outline) center = center + point;
        center = center / static_cast<double>(outline.size());
        Vec2 offset{};
        if (followExplodedParts) {
            const std::optional<Vec2> found = explosionOffsetAt(app, center, modelCenter);
            if (!found) continue; // The mark was removed by a later cut.
            offset = *found;
        }
        // Editor-only overlay: the exported STL/3MF keeps the engraved
        // geometry and receives no display color or material assignment.
        glColor4f(0.98f, 0.18f, 0.68f, 0.82f);
        glBegin(GL_POLYGON);
        for (Vec2 point : outline) glVertex3d(point.x + offset.x, point.y + offset.y, z);
        glEnd();
        glColor4f(1.0f, 0.82f, 0.96f, 1.0f);
        glBegin(GL_LINE_LOOP);
        for (Vec2 point : outline) glVertex3d(point.x + offset.x, point.y + offset.y, z + 0.002);
        glEnd();
    }
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void renderScene(App& app, int fbWidth, int fbHeight, int winWidth) {
    (void)winWidth;
    const int vx = 0;
    const int vw = std::max(1, fbWidth - vx);
    const int vh = std::max(1, fbHeight);
    glViewport(vx, 0, vw, vh);
    glClearColor(0.105f, 0.115f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const double aspect = static_cast<double>(vw) / vh;
    if (!app.loaded) return;

    if (!app.source) return;
    const Vec3 center3 = app.source->bounds.center();
    const Vec3 target{center3.x + app.camera.pan.x, center3.y + app.camera.pan.y, center3.z};
    glMatrixMode(GL_PROJECTION);
    if (app.camera.top) {
        const double halfH = app.camera.topHeight * 0.5;
        const Mat4 p = ortho(-halfH * aspect, halfH * aspect, -halfH, halfH,
                             -app.source->bounds.diagonal() * 5.0 - 10.0,
                              app.source->bounds.diagonal() * 5.0 + 10.0);
        glLoadMatrixd(p.m);
        const Mat4 v = lookAt({target.x, target.y, center3.z + std::max(app.source->bounds.diagonal(), 10.0)},
                              target, {0.0, 1.0, 0.0});
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixd(v.m);
    } else {
        const Mat4 p = perspective(40.0, aspect, 0.1, std::max(app.camera.orbitDistance * 10.0, 1000.0));
        glLoadMatrixd(p.m);
        const double yaw = app.camera.yaw * std::numbers::pi / 180.0;
        const double pitch = app.camera.pitch * std::numbers::pi / 180.0;
        const Vec3 eye = target + Vec3{
            app.camera.orbitDistance * std::cos(pitch) * std::cos(yaw),
            app.camera.orbitDistance * std::cos(pitch) * std::sin(yaw),
            app.camera.orbitDistance * std::sin(pitch)};
        const Mat4 v = lookAt(eye, target, {0.0, 0.0, 1.0});
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixd(v.m);
    }

    setupLighting();
    glPolygonMode(GL_FRONT_AND_BACK, app.wireframe ? GL_LINE : GL_FILL);
    drawGrid(app.source->bounds);

    const float modelAlpha = static_cast<float>(std::clamp(
        1.0 - app.modelTransparency / 100.0, 0.15, 1.0));
    const bool transparentModel = modelAlpha < 0.999f;
    if (transparentModel) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    if (app.showSource && !app.cuts.empty()) {
        if (app.materials.size() > 1)
            app.sourceGL.drawMaterials(app.materials, modelAlpha, 0.78f);
        else {
            glColor4f(0.66f, 0.70f, 0.76f, modelAlpha);
            app.sourceGL.draw();
        }
    }
    size_t colorIndex = 0;
    for (const auto& part : app.parts) {
        if (!part.active) continue;
        if (!part.mesh) continue;
        const Vec2 explosionOffset = partExplosionOffset(app, part, center3);
        glPushMatrix();
        glTranslated(explosionOffset.x, explosionOffset.y, 0.0);
        if (app.materials.size() > 1) {
            // Filament colors are data, not UI decoration. Brightening the
            // selected part and darkening its siblings made an unchanged
            // palette look different immediately after a cut.
            part.gl.drawMaterials(app.materials, modelAlpha, 1.0f);
        } else {
            const float boost = part.id == app.selectedPartId ? 1.12f : 0.88f;
            const auto& color = kPartColors[colorIndex % kPartColors.size()];
            glColor4f(std::min(color[0] * boost, 1.0f),
                      std::min(color[1] * boost, 1.0f),
                      std::min(color[2] * boost, 1.0f), modelAlpha);
            part.gl.draw();
        }
        if (app.showMeshIssues && part.id == app.selectedPartId)
            drawMeshIssueEdges(app.meshDiagnostics);
        glPopMatrix();
        ++colorIndex;
    }
    if (transparentModel) glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    const double overlayZ = app.source->bounds.max.z + std::max(0.12, app.source->bounds.size().z * 0.03);
    const double overlayHandle = app.camera.top ? std::max(app.camera.topHeight / std::max(vh, 1) * 5.0, 0.35)
                                                : std::max(app.source->bounds.diagonal() * 0.008, 0.35);
    drawPrintBed(app, overlayZ + 0.08, overlayHandle);

    if (!app.measurePoints.empty()) {
        const double z = overlayZ + 0.16;
        glDisable(GL_LIGHTING);
        glColor3f(0.28f, 1.0f, 0.55f);
        glLineWidth(3.0f);
        if (app.measurePoints.size() == 2) {
            glBegin(GL_LINES);
            glVertex3d(app.measurePoints[0].x, app.measurePoints[0].y, z);
            glVertex3d(app.measurePoints[1].x, app.measurePoints[1].y, z);
            glEnd();
        }
        glEnable(GL_LIGHTING);
        for (Vec2 point : app.measurePoints)
            drawSquare(point, overlayHandle * 0.75, z + 0.01, 0.28f, 1.0f, 0.55f);
    }

    if (!app.cuts.empty()) {
        std::unordered_set<int> drawnCutIds;
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.0f);
        const double z = overlayZ + 0.11;
        for (const CutRecord& cut : app.cuts) {
            if (cut.points.size() < 2 || !drawnCutIds.insert(cut.id).second) continue;
            const auto& color = cutColor(cut.id);
            glColor3f(color[0], color[1], color[2]);
            glBegin(GL_LINE_STRIP);
            for (Vec2 point : cut.points) glVertex3d(point.x, point.y, z);
            glEnd();
            const std::vector<ConnectorPreview>& appliedPreview = appliedConnectorPreview(app, cut);
            glLineWidth(1.5f);
            for (size_t connectorIndex = 0; connectorIndex < appliedPreview.size(); ++connectorIndex) {
                const ConnectorPreview& connector = appliedPreview[connectorIndex];
                float connectorRed = color[0];
                float connectorGreen = color[1];
                float connectorBlue = color[2];
                if (!connector.valid && connector.partiallyValid) {
                    connectorRed = 1.0f; connectorGreen = 0.48f; connectorBlue = 0.08f;
                } else if (!connector.valid) {
                    connectorRed = 1.0f; connectorGreen = 0.12f; connectorBlue = 0.08f;
                } else if (connector.intersectsExistingCut) {
                    connectorRed = 0.88f; connectorGreen = 0.30f; connectorBlue = 1.0f;
                } else if (connector.thinWall) {
                    connectorRed = 1.0f; connectorGreen = 0.86f; connectorBlue = 0.12f;
                }
                glColor3f(connectorRed, connectorGreen, connectorBlue);
                for (const std::vector<Vec2>* outline : {&appliedPreview[connectorIndex].maleOutline,
                                                         &appliedPreview[connectorIndex].socketOutline}) {
                    glBegin(GL_LINE_LOOP);
                    for (Vec2 point : *outline) glVertex3d(point.x, point.y, z + 0.01);
                    glEnd();
                }
                const bool dragged = app.draggedAppliedCutId == cut.id &&
                    app.draggedAppliedConnector == static_cast<int>(connectorIndex);
                drawSquare(appliedPreview[connectorIndex].center, overlayHandle * 0.58, z + 0.03,
                           dragged ? 1.0f : connectorRed,
                           dragged ? 1.0f : connectorGreen,
                           dragged ? 1.0f : connectorBlue);
                glDisable(GL_LIGHTING);
                glDisable(GL_DEPTH_TEST);
                glColor3f(color[0], color[1], color[2]);
            }
            if (cut.settings.assemblyMarks) {
                const AssemblyMarkPreview appliedMark = makeAssemblyMarkPreview(
                    cut.points, cut.settings, cut.settings.assemblyMarkCode);
                drawAssemblyMarkOverlay(app, appliedMark, center3, z + 0.055, true);
                glDisable(GL_LIGHTING);
                glDisable(GL_DEPTH_TEST);
                glColor3f(color[0], color[1], color[2]);
            }
        }
        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LIGHTING);
    }

    if (!app.cutPoints.empty()) {
        glDisable(GL_LIGHTING);
        const double z = app.source->bounds.max.z + std::max(0.08, app.source->bounds.size().z * 0.02);
        glLineWidth(3.0f);
        glColor3f(1.0f, 0.15f, 0.12f);
        glBegin(GL_LINE_STRIP);
        for (Vec2 p : app.cutPoints) glVertex3d(p.x, p.y, z);
        glEnd();
        glEnable(GL_LIGHTING);

        const double handleHalf = app.camera.top ? std::max(app.camera.topHeight / std::max(vh, 1) * 5.0, 0.35)
                                                 : std::max(app.source->bounds.diagonal() * 0.008, 0.35);
        for (size_t index = 0; index < app.cutPoints.size(); ++index) {
            const bool locked = index < app.cutPointLocked.size() && app.cutPointLocked[index];
            drawSquare(app.cutPoints[index], handleHalf, z + 0.03,
                       locked ? 1.0f : 1.0f, locked ? 0.72f : 0.10f, locked ? 0.08f : 0.08f);
            if (locked) {
                glDisable(GL_LIGHTING);
                glColor3f(0.25f, 0.12f, 0.02f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex3d(app.cutPoints[index].x - handleHalf * 0.55, app.cutPoints[index].y, z + 0.05);
                glVertex3d(app.cutPoints[index].x + handleHalf * 0.55, app.cutPoints[index].y, z + 0.05);
                glVertex3d(app.cutPoints[index].x, app.cutPoints[index].y - handleHalf * 0.55, z + 0.05);
                glVertex3d(app.cutPoints[index].x, app.cutPoints[index].y + handleHalf * 0.55, z + 0.05);
                glEnd();
                glEnable(GL_LIGHTING);
            }
        }

        if (app.hasCompleteCut()) {
            const double arrowScale = std::max(std::min(app.settings.depth * 0.65, app.camera.topHeight * 0.06), handleHalf * 2.5);
            for (size_t connectorIndex = 0; connectorIndex < app.preview.size(); ++connectorIndex) {
                const auto& c = app.preview[connectorIndex];
                drawOutline(c.socketOutline, z + 0.01, c.valid, c.partiallyValid,
                            c.thinWall, c.intersectsExistingCut, true);
                drawOutline(c.maleOutline, z + 0.02, c.valid, c.partiallyValid,
                            c.thinWall, c.intersectsExistingCut, false);
                const bool locked = connectorIndex < app.placements.size() && app.placements[connectorIndex].locked;
                const bool selected = static_cast<int>(connectorIndex) == app.selectedConnector;
                drawSquare(c.center, handleHalf * 0.72, z + 0.04,
                           selected ? 1.00f : (locked ? 1.00f : (c.partiallyValid ? 1.00f : (!c.valid ? 0.95f : (c.intersectsExistingCut ? 0.88f : (c.thinWall ? 1.00f : 0.10f))))),
                           selected ? 1.00f : (locked ? 0.72f : (c.partiallyValid ? 0.56f : (!c.valid ? 0.18f : (c.intersectsExistingCut ? 0.30f : (c.thinWall ? 0.86f : 0.75f))))),
                           selected ? 1.00f : (locked ? 0.08f : (c.partiallyValid ? 0.08f : (!c.valid ? 0.12f : (c.intersectsExistingCut ? 1.00f : (c.thinWall ? 0.12f : 1.00f))))));
                drawDirectionArrow(c, z + 0.05, arrowScale);
            }
            if (app.settings.assemblyMarks) {
                const AssemblyMarkPreview mark = makeAssemblyMarkPreview(
                    app.cutPoints, app.settings, app.activeAssemblyMarkCode());
                drawAssemblyMarkOverlay(app, mark, center3, z + 0.07, false);
            }
        }
    }
}

Vec2 mouseToTopWorld(const App& app, double mouseX, double mouseY, int winWidth, int winHeight) {
    const double vx = 0.0;
    const double vw = std::max(1.0, static_cast<double>(winWidth) - vx);
    const double vh = std::max(1.0, static_cast<double>(winHeight));
    const double aspect = vw / vh;
    const double nx = ((mouseX - vx) / vw) * 2.0 - 1.0;
    const double ny = 1.0 - (mouseY / vh) * 2.0;
    const Vec3 c = app.source->bounds.center();
    const double halfH = app.camera.topHeight * 0.5;
    return {c.x + app.camera.pan.x + nx * halfH * aspect,
            c.y + app.camera.pan.y + ny * halfH};
}

std::optional<Vec2> mouseTo3dPlane(const App& app, double mouseX, double mouseY,
                                   int winWidth, int winHeight, double planeZ) {
    const double viewportX = 0.0;
    const double viewportWidth = std::max(1.0, static_cast<double>(winWidth) - viewportX);
    const double viewportHeight = std::max(1.0, static_cast<double>(winHeight));
    const double aspect = viewportWidth / viewportHeight;
    const double normalizedX = ((mouseX - viewportX) / viewportWidth) * 2.0 - 1.0;
    const double normalizedY = 1.0 - (mouseY / viewportHeight) * 2.0;
    const Vec3 center = app.source->bounds.center();
    const Vec3 target{center.x + app.camera.pan.x, center.y + app.camera.pan.y, center.z};
    const double yaw = app.camera.yaw * std::numbers::pi / 180.0;
    const double pitch = app.camera.pitch * std::numbers::pi / 180.0;
    const Vec3 eye = target + Vec3{
        app.camera.orbitDistance * std::cos(pitch) * std::cos(yaw),
        app.camera.orbitDistance * std::cos(pitch) * std::sin(yaw),
        app.camera.orbitDistance * std::sin(pitch)};
    const Vec3 forward = normalized(target - eye);
    const Vec3 right = normalized(cross(forward, {0.0, 0.0, 1.0}));
    const Vec3 viewUp = normalized(cross(right, forward));
    const double tangent = std::tan(20.0 * std::numbers::pi / 180.0);
    const Vec3 ray = normalized(forward + right * (normalizedX * tangent * aspect) +
                                viewUp * (normalizedY * tangent));
    if (std::abs(ray.z) < 1e-9) return std::nullopt;
    const double distance = (planeZ - eye.z) / ray.z;
    if (distance <= 0.0) return std::nullopt;
    const Vec3 point = eye + ray * distance;
    return Vec2{point.x, point.y};
}

double hitRadiusWorld(const App& app, int winHeight, double pixels) {
    const double visibleHeight = app.camera.top
        ? app.camera.topHeight
        : 2.0 * app.camera.orbitDistance * std::tan(20.0 * std::numbers::pi / 180.0);
    return std::max(visibleHeight / std::max(winHeight, 1) * pixels, 0.5);
}
