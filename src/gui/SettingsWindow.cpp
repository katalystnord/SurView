#include "gui/SettingsWindow.h"

#include "gui/NoteLabel.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

SettingsWindow::SettingsWindow(std::function<RunDefaults()> current, QWidget *parent)
    : QDialog(parent), m_current(std::move(current))
{
    setWindowTitle(tr("Settings"));
    resize(640, 620);

    auto *column = new QVBoxLayout(this);

    auto *heading = new QLabel(tr("<b>Defaults for new projects</b>"), this);
    column->addWidget(heading);

    auto *explanation = new NoteLabel(this);
    explanation->setText(
        tr("A new project, and SurView when it starts, begins from these settings. "
           "A project you open keeps the settings it was saved with, whatever is "
           "here, so it measures the same way on any machine. Each setting is "
           "changed in the Analysis panel, in the section named below; to make "
           "the panel's current settings the defaults, keep them with the button "
           "underneath."));
    column->addWidget(explanation);

    m_status = new NoteLabel(this);
    column->addWidget(m_status);

    m_table = new QTreeWidget(this);
    m_table->setColumnCount(3);
    m_table->setHeaderLabels({tr("Setting"), tr("New projects start at"), tr("SurView's own")});
    m_table->setRootIsDecorated(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->header()->setStretchLastSection(false);
    column->addWidget(m_table, 1);

    m_keep = new QPushButton(tr("Use the current project's settings as the defaults"), this);
    m_keep->setToolTip(tr("Every setting on the Analysis panel, as it stands now, "
                          "becomes where a new project starts."));
    connect(m_keep, &QPushButton::clicked, this, &SettingsWindow::keepCurrent);
    m_restore = new QPushButton(tr("Restore SurView's own defaults"), this);
    m_restore->setToolTip(tr("Forgets the saved defaults. The open project keeps its "
                             "settings; the next new project starts from SurView's own."));
    connect(m_restore, &QPushButton::clicked, this, &SettingsWindow::restoreOwn);
    auto *actions = new QHBoxLayout;
    actions->addWidget(m_keep);
    actions->addWidget(m_restore);
    actions->addStretch(1);
    column->addLayout(actions);

    m_where = new NoteLabel(this);
    column->addWidget(m_where);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    column->addWidget(buttons);

    refresh();
}

void SettingsWindow::refresh()
{
    const QString path = defaultsPath();
    const DefaultsLoad loaded = loadDefaults(path);
    const QVector<SettingLine> lines = settingLines(loaded.defaults);

    int differing = 0;
    for (const SettingLine &line : lines)
        differing += line.differs();

    if (!loaded.problem.isEmpty()) {
        m_status->setText(loaded.problem);
    } else if (!loaded.saved) {
        m_status->setText(tr("Nothing is saved, so new projects start from SurView's own "
                             "defaults."));
    } else if (differing == 0) {
        m_status->setText(tr("Saved defaults, all the same as SurView's own."));
    } else {
        m_status->setText(differing == 1
                              ? tr("Saved defaults: 1 setting differs from SurView's own, "
                                   "shown in bold.")
                              : tr("Saved defaults: %1 settings differ from SurView's own, "
                                   "shown in bold.")
                                    .arg(differing));
    }

    m_table->clear();
    QString section;
    for (const SettingLine &line : lines) {
        if (line.section != section) {
            section = line.section;
            auto *heading = new QTreeWidgetItem(m_table, {section});
            QFont font = heading->font(0);
            font.setBold(true);
            heading->setFont(0, font);
            heading->setFirstColumnSpanned(true);
            heading->setFlags(Qt::ItemIsEnabled);
        }
        auto *item = new QTreeWidgetItem(m_table, {line.label, line.value, line.builtIn});
        item->setFlags(Qt::ItemIsEnabled);
        if (line.differs()) {
            QFont font = item->font(1);
            font.setBold(true);
            item->setFont(0, font);
            item->setFont(1, font);
        }
        item->setToolTip(0, tr("Changed in the Analysis panel, under %1.").arg(line.section));
    }

    m_restore->setEnabled(loaded.saved || !loaded.problem.isEmpty());
    m_where->setText(tr("Kept in %1.").arg(QDir::toNativeSeparators(path)));
}

void SettingsWindow::keepCurrent()
{
    const QString refusal = saveDefaults(defaultsPath(), m_current());
    if (!refusal.isEmpty()) {
        QMessageBox::warning(this, tr("Could not keep the defaults"), refusal);
        return;
    }
    refresh();
    emit defaultsChanged(tr("New projects now start from this project's settings."));
}

void SettingsWindow::restoreOwn()
{
    const QString refusal = forgetDefaults(defaultsPath());
    if (!refusal.isEmpty()) {
        QMessageBox::warning(this, tr("Could not restore the defaults"), refusal);
        return;
    }
    refresh();
    emit defaultsChanged(tr("New projects now start from SurView's own defaults."));
}
