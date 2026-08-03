#pragma once

enum class ConnectorType { Dovetail, StraightTab, Puzzle, RoundPin };

struct DovetailSettings {
    enum class Distribution { Count, Spacing };
    Distribution distribution = Distribution::Count;
    int count = 3;
    double spacing = 35.0;
    double endMargin = 12.0;
    double neckWidth = 8.0;
    double headWidth = 12.0;
    double depth = 8.0;
    double embed = 1.5;
    double clearance = 0.20;
    bool leadChamfer = false;
    double chamferWidth = 0.60;
    double chamferAngle = 45.0;
    // Quality & assembly options.  The minimum wall is a warning threshold;
    // it does not silently resize or discard an otherwise valid connector.
    bool validateWallThickness = true;
    double minimumWall = 1.20;
    bool assemblyMarks = true;
    int assemblyMarkCode = 1;
    double assemblyMarkPosition = 0.5;
    double assemblyMarkSize = 6.0;
    double assemblyMarkDepth = 0.35;
    bool assemblyMarkPositionsCustom = false;
    double assemblyMarkLeftX = 0.0;
    double assemblyMarkLeftY = 0.0;
    double assemblyMarkRightX = 0.0;
    double assemblyMarkRightY = 0.0;
    bool maleOnLeft = true;
    bool validateInsideModel = true;
    ConnectorType type = ConnectorType::Dovetail;
};

// Position is normalized over the complete arc length of the cut polyline.
// maleOnLeft describes which local side of the directed segment owns the male connector.
struct ConnectorPlacement {
    double position = 0.5;
    bool maleOnLeft = true;
    bool locked = false;
    ConnectorType type = ConnectorType::Dovetail;
    double neckWidth = -1.0;
    double headWidth = -1.0;
    double depth = -1.0;
    double embed = -1.0;
    double clearance = -1.0;
    int leadChamfer = -1;
    double chamferWidth = -1.0;
    double chamferAngle = -1.0;
};
