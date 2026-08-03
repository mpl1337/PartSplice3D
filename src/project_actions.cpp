#include "project_actions.h"

#include "file_dialogs.h"
#include "project_workflow.h"

#include <string>

void performProjectAction(App& app, PendingProjectAction action, double aspect) {
    switch (action) {
        case PendingProjectAction::NewProject:
            app.newProject();
            break;
        case PendingProjectAction::OpenModel:
            if (auto path = openModelDialog()) loadFileOrProject(app, *path, aspect);
            break;
        case PendingProjectAction::OpenSpecific: {
            const std::filesystem::path path = app.pendingOpenPath;
            app.pendingOpenPath.clear();
            if (!path.empty()) loadFileOrProject(app, path, aspect);
            break;
        }
        case PendingProjectAction::CloseProject:
            app.newProject();
            app.status = "Projekt geschlossen. Über Datei / Projekt kann ein neues Modell geöffnet werden.";
            break;
        case PendingProjectAction::None:
            break;
    }
}

void requestOpenPath(App& app, const std::filesystem::path& path, double aspect) {
    app.pendingOpenPath = path;
    requestProjectAction(app, PendingProjectAction::OpenSpecific, aspect);
}

void requestProjectAction(App& app, PendingProjectAction action, double aspect) {
    if (app.busy()) {
        app.status = "Bitte warten, bis der laufende Vorgang abgeschlossen oder abgebrochen ist.";
        return;
    }
    if (app.pendingAppliedCutRebuildId >= 0) {
        app.status = "Zuerst den geänderten Schnitt mit ‚Neu schneiden‘ übernehmen oder mit Strg+Z verwerfen.";
        return;
    }
    if (app.loaded && app.dirty) {
        app.pendingProjectAction = action;
        app.showProjectConfirmation = true;
        return;
    }
    performProjectAction(app, action, aspect);
}

void requestBeginCut(App& app, CutMode mode) {
    if (app.pendingAppliedCutRebuildId >= 0) {
        app.status = "Zuerst den geänderten Schnitt mit ‚Neu schneiden‘ übernehmen oder mit Strg+Z verwerfen.";
        return;
    }
    if (!app.cutPoints.empty()) {
        app.pendingCutMode = mode;
        app.showPendingCutPrompt = true;
        return;
    }
    app.captureUndo("Neue Schnittlinie beginnen");
    app.beginCut(mode);
}

void cancelCurrentCut(App& app) {
    if (!app.drawingLine && app.cutPoints.empty()) {
        app.status = "Es ist kein noch nicht angewendeter Schnitt vorhanden.";
        return;
    }
    if (app.editingCutOperationId >= 0) {
        const int operationId = app.editingCutOperationId;
        while (!app.undoStack.empty() &&
               !(app.undoStack.back().kind == UndoKind::DeletedCut &&
                 app.undoStack.back().operationId == operationId)) {
            app.undoStack.pop_back();
        }
        if (!app.undoStack.empty()) {
            app.undoLast();
            app.editingCutOperationId = -1;
            app.status = "Bearbeitung von Schnitt " + std::to_string(operationId) +
                         " abgebrochen. Das vorherige Schnittergebnis wurde wiederhergestellt.";
            return;
        }
    }
    app.captureUndo("Schnitt abbrechen");
    app.clearCurrentCut();
    app.dirty = true;
    app.status = "Der noch nicht angewendete Schnitt wurde abgebrochen und verworfen.";
}
