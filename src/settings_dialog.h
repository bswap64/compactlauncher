#pragma once
#include "launcher.h"
#include <QDialog>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QSettings>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent, QSettings* cfg)
        : QDialog(parent), m_cfg(cfg)
    {
        setWindowTitle("Settings");
        setFixedWidth(440);

        auto* main = new QVBoxLayout(this);
        auto* form = new QFormLayout();

        m_useCustomJava = new QCheckBox("Use custom Java binary");
        m_useCustomJava->setChecked(cfg->value(CFG_USE_CUSTOM_JAVA, DEFAULT_USE_CUSTOM_JAVA).toBool());
        m_javaPath = new QLineEdit(cfg->value(CFG_JAVA_PATH, DEFAULT_JAVA_PATH).toString());
        m_javaPath->setEnabled(m_useCustomJava->isChecked());
        form->addRow(m_useCustomJava, m_javaPath);

        m_useCustomArgs = new QCheckBox("Use custom JVM arguments");
        m_useCustomArgs->setChecked(cfg->value(CFG_USE_CUSTOM_ARGS, DEFAULT_USE_CUSTOM_ARGS).toBool());
        m_customArgs = new QLineEdit(cfg->value(CFG_CUSTOM_ARGS, DEFAULT_JVM_ARGS_NEW).toString());
        m_customArgs->setEnabled(m_useCustomArgs->isChecked());
        form->addRow(m_useCustomArgs, m_customArgs);

        m_threads = new QSpinBox();
        m_threads->setRange(1, 64);
        m_threads->setValue(cfg->value(CFG_DL_THREADS, DEFAULT_DL_THREADS).toInt());
        form->addRow("Download threads:", m_threads);

        main->addLayout(form);

        auto* line = new QFrame();
        line->setFrameShape(QFrame::HLine);
        main->addWidget(line);

        m_asyncDl    = new QCheckBox("Multi-threaded downloading");
        m_dlMissing  = new QCheckBox("Download missing libraries on launch");
        m_keepOpen   = new QCheckBox("Keep launcher open after game starts");
        m_saveLaunch = new QCheckBox("Save full launch string to launch_string.txt");

        m_asyncDl->setChecked(cfg->value(CFG_ASYNC_DL, DEFAULT_ASYNC_DL).toBool());
        m_dlMissing->setChecked(cfg->value(CFG_DL_MISSING_LIBS, DEFAULT_DL_MISSING).toBool());
        m_keepOpen->setChecked(cfg->value(CFG_KEEP_OPEN, DEFAULT_KEEP_OPEN).toBool());
        m_saveLaunch->setChecked(cfg->value(CFG_SAVE_LAUNCH_STR, DEFAULT_SAVE_LAUNCH).toBool());

        main->addWidget(m_asyncDl);
        main->addWidget(m_dlMissing);
        main->addWidget(m_keepOpen);
        main->addWidget(m_saveLaunch);

        auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        main->addWidget(btns);

        connect(m_useCustomJava, &QCheckBox::toggled, m_javaPath, &QLineEdit::setEnabled);
        connect(m_useCustomArgs, &QCheckBox::toggled, m_customArgs, &QLineEdit::setEnabled);
        connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::onOk);
        connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

private slots:
    void onOk() {
        m_cfg->setValue(CFG_USE_CUSTOM_JAVA, m_useCustomJava->isChecked());
        m_cfg->setValue(CFG_JAVA_PATH,       m_javaPath->text());
        m_cfg->setValue(CFG_USE_CUSTOM_ARGS, m_useCustomArgs->isChecked());
        m_cfg->setValue(CFG_CUSTOM_ARGS,     m_customArgs->text());
        m_cfg->setValue(CFG_DL_THREADS,      m_threads->value());
        m_cfg->setValue(CFG_ASYNC_DL,        m_asyncDl->isChecked());
        m_cfg->setValue(CFG_DL_MISSING_LIBS, m_dlMissing->isChecked());
        m_cfg->setValue(CFG_KEEP_OPEN,       m_keepOpen->isChecked());
        m_cfg->setValue(CFG_SAVE_LAUNCH_STR, m_saveLaunch->isChecked());
        m_cfg->sync();
        accept();
    }

private:
    QSettings*  m_cfg;
    QCheckBox*  m_useCustomJava;
    QCheckBox*  m_useCustomArgs;
    QCheckBox*  m_asyncDl;
    QCheckBox*  m_dlMissing;
    QCheckBox*  m_keepOpen;
    QCheckBox*  m_saveLaunch;
    QLineEdit*  m_javaPath;
    QLineEdit*  m_customArgs;
    QSpinBox*   m_threads;
};
