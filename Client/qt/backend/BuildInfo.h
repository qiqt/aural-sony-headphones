#pragma once

#include <QObject>
#include <QString>

// Which build this is, in the build's own words.
//
// A release candidate has to be traceable to the source it came from: a bug report that says
// "v2.1.0" is not actionable when several builds share that version. The revision is baked in
// at configure time from git (AURAL_BUILD_REVISION) and shown in About, and the packaging step
// writes the same string into BUILD-INFO.txt next to the executable.
//
// "unknown" is a real answer: a build from a source archive with no .git has nothing honest to
// report, and inventing something would defeat the point.
class BuildInfo : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString revision READ revision CONSTANT)
    Q_PROPERTY(QString branch READ branch CONSTANT)
    // True when the working tree had uncommitted changes at configure time, so the revision
    // alone does not describe this binary.
    Q_PROPERTY(bool modified READ modified CONSTANT)
    // "2.1.0 (d280814 on multi-device, modified)" - one line, ready to paste into a report.
    Q_PROPERTY(QString summary READ summary CONSTANT)

public:
    explicit BuildInfo(QObject* parent = nullptr) : QObject(parent) {}

    static QString version();
    static QString revision();
    static QString branch();
    static bool modified();
    static QString summary();
};
