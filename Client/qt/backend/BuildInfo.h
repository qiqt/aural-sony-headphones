#pragma once

#include <QObject>
#include <QString>

// Version and Git revision captured at configure time, shown in About and BUILD-INFO.txt.
// Source archives without .git use "unknown" for the revision.
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
