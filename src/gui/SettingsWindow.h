#pragma once

#include "core/Defaults.h"

#include <QDialog>

#include <functional>

class NoteLabel;
class QPushButton;
class QTreeWidget;

// File > Settings: what a new project starts from.
//
// It shows the defaults and does not edit them. Every run setting already has
// one control, in the Analysis panel, and a second control for the same value
// here would be two places to look for one number. A default is made by setting
// the panel and keeping its settings, which is also the only way to make one
// that has been seen working. See core/Defaults.h for why a default never
// reaches an opened project.
class SettingsWindow : public QDialog
{
    Q_OBJECT

public:
    // `current` gives the settings on the Analysis panel at the moment they
    // are kept.
    SettingsWindow(std::function<RunDefaults()> current, QWidget *parent = nullptr);

    // Reads the saved defaults again and redraws.
    void refresh();

signals:
    // The defaults were kept or forgotten, with a sentence saying which.
    void defaultsChanged(const QString &what);

private:
    void keepCurrent();
    void restoreOwn();

    std::function<RunDefaults()> m_current;
    NoteLabel *m_status = nullptr;
    NoteLabel *m_where = nullptr;
    QTreeWidget *m_table = nullptr;
    QPushButton *m_keep = nullptr;
    QPushButton *m_restore = nullptr;
};
