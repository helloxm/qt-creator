// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qbsbuildstep.h"

#include "qbsbuildconfiguration.h"
#include "qbsproject.h"
#include "qbsprojectmanagerconstants.h"
#include "qbsprojectmanagertr.h"
#include "qbssession.h"
#include "qbssettings.h"

#include <coreplugin/icore.h>

#include <projectexplorer/buildsteplist.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/projectexplorertr.h>
#include <projectexplorer/target.h>

#include <qtsupport/qtkitinformation.h>
#include <qtsupport/qtversionmanager.h>

#include <utils/algorithm.h>
#include <utils/aspects.h>
#include <utils/guard.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/outputformatter.h>
#include <utils/pathchooser.h>
#include <utils/process.h>
#include <utils/qtcassert.h>
#include <utils/variablechooser.h>

#include <QCheckBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QThread>

// --------------------------------------------------------------------
// Constants:
// --------------------------------------------------------------------

const char QBS_CONFIG[] = "Qbs.Configuration";
const char QBS_KEEP_GOING[] = "Qbs.DryKeepGoing";
const char QBS_MAXJOBCOUNT[] = "Qbs.MaxJobs";
const char QBS_SHOWCOMMANDLINES[] = "Qbs.ShowCommandLines";
const char QBS_INSTALL[] = "Qbs.Install";
const char QBS_CLEAN_INSTALL_ROOT[] = "Qbs.CleanInstallRoot";

using namespace ProjectExplorer;
using namespace QtSupport;
using namespace Utils;

namespace QbsProjectManager {
namespace Internal {

class ArchitecturesAspect : public Utils::MultiSelectionAspect
{
    Q_OBJECT
public:
    ArchitecturesAspect();

    void setKit(const ProjectExplorer::Kit *kit) { m_kit = kit; }
    void addToLayout(Layouting::LayoutItem &parent) override;
    QStringList selectedArchitectures() const;
    void setSelectedArchitectures(const QStringList& architectures);
    bool isManagedByTarget() const { return m_isManagedByTarget; }

private:
    void setVisibleDynamic(bool visible);

    const ProjectExplorer::Kit *m_kit = nullptr;
    QMap<QString, QString> m_abisToArchMap;
    bool m_isManagedByTarget = false;
};

ArchitecturesAspect::ArchitecturesAspect()
{
    m_abisToArchMap = {
        {ProjectExplorer::Constants::ANDROID_ABI_ARMEABI_V7A, "armv7a"},
        {ProjectExplorer::Constants::ANDROID_ABI_ARM64_V8A, "arm64"},
        {ProjectExplorer::Constants::ANDROID_ABI_X86, "x86"},
        {ProjectExplorer::Constants::ANDROID_ABI_X86_64, "x86_64"}};
    setAllValues(m_abisToArchMap.keys());
}

void ArchitecturesAspect::addToLayout(Layouting::LayoutItem &parent)
{
    MultiSelectionAspect::addToLayout(parent);
    const auto changeHandler = [this] {
        const QtVersion *qtVersion = QtKitAspect::qtVersion(m_kit);
        if (!qtVersion) {
            setVisibleDynamic(false);
            return;
        }
        const Abis abis = qtVersion->qtAbis();
        if (abis.size() <= 1) {
            setVisibleDynamic(false);
            return;
        }
        bool isAndroid = Utils::anyOf(abis, [](const Abi &abi) {
            return abi.osFlavor() == Abi::OSFlavor::AndroidLinuxFlavor;
        });
        if (!isAndroid) {
            setVisibleDynamic(false);
            return;
        }

        setVisibleDynamic(true);
    };
    connect(KitManager::instance(), &KitManager::kitsChanged, this, changeHandler);
    connect(this, &ArchitecturesAspect::changed, this, changeHandler);
    changeHandler();
}

QStringList ArchitecturesAspect::selectedArchitectures() const
{
    QStringList architectures;
    for (const auto &abi : value()) {
        if (m_abisToArchMap.contains(abi))
            architectures << m_abisToArchMap[abi];
    }
    return architectures;
}

void ArchitecturesAspect::setVisibleDynamic(bool visible)
{
    MultiSelectionAspect::setVisible(visible);
    m_isManagedByTarget = visible;
}

void ArchitecturesAspect::setSelectedArchitectures(const QStringList& architectures)
{
    QStringList newValue;
    for (auto i = m_abisToArchMap.constBegin(); i != m_abisToArchMap.constEnd(); ++i) {
        if (architectures.contains(i.value()))
            newValue << i.key();
    }
    if (newValue != value())
        setValue(newValue);
}

class QbsBuildStepConfigWidget : public QWidget
{
    Q_OBJECT
public:
    QbsBuildStepConfigWidget(QbsBuildStep *step);

private:
    void updateState();
    void updatePropertyEdit(const QVariantMap &data);

    void changeUseDefaultInstallDir(bool useDefault);
    void changeInstallDir();
    void applyCachedProperties();

    QbsBuildStep *qbsStep() const;

    bool validateProperties(Utils::FancyLineEdit *edit, QString *errorMessage);

    class Property
    {
    public:
        Property() = default;
        Property(const QString &n, const QString &v, const QString &e) :
            name(n), value(v), effectiveValue(e)
        {}
        bool operator==(const Property &other) const
        {
            return name == other.name
                    && value == other.value
                    && effectiveValue == other.effectiveValue;
        }

        QString name;
        QString value;
        QString effectiveValue;
    };

    QbsBuildStep *m_qbsStep;
    QList<Property> m_propertyCache;
    Guard m_ignoreChanges;

    FancyLineEdit *propertyEdit;
    PathChooser *installDirChooser;
    QCheckBox *defaultInstallDirCheckBox;
};

// --------------------------------------------------------------------
// QbsBuildStep:
// --------------------------------------------------------------------

QbsBuildStep::QbsBuildStep(BuildStepList *bsl, Utils::Id id) :
    BuildStep(bsl, id)
{
    setDisplayName(QbsProjectManager::Tr::tr("Qbs Build"));
    setSummaryText(QbsProjectManager::Tr::tr("<b>Qbs:</b> %1").arg("build"));

    setQbsConfiguration(QVariantMap());

    auto qbsBuildConfig = qobject_cast<QbsBuildConfiguration *>(buildConfiguration());
    QTC_CHECK(qbsBuildConfig);
    connect(this, &QbsBuildStep::qbsConfigurationChanged,
            qbsBuildConfig, &QbsBuildConfiguration::qbsConfigurationChanged);

    m_buildVariant = addAspect<SelectionAspect>();
    m_buildVariant->setDisplayName(QbsProjectManager::Tr::tr("Build variant:"));
    m_buildVariant->setDisplayStyle(SelectionAspect::DisplayStyle::ComboBox);
    m_buildVariant->addOption({ProjectExplorer::Tr::tr("Debug"), {}, Constants::QBS_VARIANT_DEBUG});
    m_buildVariant->addOption({ProjectExplorer::Tr::tr("Release"), {},
                               Constants::QBS_VARIANT_RELEASE});
    m_buildVariant->addOption({ProjectExplorer::Tr::tr("Profile"), {},
                               Constants::QBS_VARIANT_PROFILING});

    m_selectedAbis = addAspect<ArchitecturesAspect>();
    m_selectedAbis->setLabelText(QbsProjectManager::Tr::tr("ABIs:"));
    m_selectedAbis->setDisplayStyle(MultiSelectionAspect::DisplayStyle::ListView);
    m_selectedAbis->setKit(target()->kit());

    m_keepGoing = addAspect<BoolAspect>();
    m_keepGoing->setSettingsKey(QBS_KEEP_GOING);
    m_keepGoing->setToolTip(
                QbsProjectManager::Tr::tr("Keep going when errors occur (if at all possible)."));
    m_keepGoing->setLabel(QbsProjectManager::Tr::tr("Keep going"),
                          BoolAspect::LabelPlacement::AtCheckBoxWithoutDummyLabel);

    m_maxJobCount = addAspect<IntegerAspect>();
    m_maxJobCount->setSettingsKey(QBS_MAXJOBCOUNT);
    m_maxJobCount->setLabel(QbsProjectManager::Tr::tr("Parallel jobs:"));
    m_maxJobCount->setToolTip(QbsProjectManager::Tr::tr("Number of concurrent build jobs."));
    m_maxJobCount->setValue(QThread::idealThreadCount());

    m_showCommandLines = addAspect<BoolAspect>();
    m_showCommandLines->setSettingsKey(QBS_SHOWCOMMANDLINES);
    m_showCommandLines->setLabel(QbsProjectManager::Tr::tr("Show command lines"),
                                 BoolAspect::LabelPlacement::AtCheckBoxWithoutDummyLabel);

    m_install = addAspect<BoolAspect>();
    m_install->setSettingsKey(QBS_INSTALL);
    m_install->setValue(true);
    m_install->setLabel(QbsProjectManager::Tr::tr("Install"), BoolAspect::LabelPlacement::AtCheckBoxWithoutDummyLabel);

    m_cleanInstallDir = addAspect<BoolAspect>();
    m_cleanInstallDir->setSettingsKey(QBS_CLEAN_INSTALL_ROOT);
    m_cleanInstallDir->setLabel(QbsProjectManager::Tr::tr("Clean install root"),
                                BoolAspect::LabelPlacement::AtCheckBoxWithoutDummyLabel);

    m_forceProbes = addAspect<BoolAspect>();
    m_forceProbes->setSettingsKey("Qbs.forceProbesKey");
    m_forceProbes->setLabel(QbsProjectManager::Tr::tr("Force probes"),
                            BoolAspect::LabelPlacement::AtCheckBoxWithoutDummyLabel);

    m_commandLine = addAspect<StringAspect>();
    m_commandLine->setDisplayStyle(StringAspect::TextEditDisplay);
    m_commandLine->setLabelText(QbsProjectManager::Tr::tr("Equivalent command line:"));
    m_commandLine->setUndoRedoEnabled(false);
    m_commandLine->setReadOnly(true);

    connect(m_maxJobCount, &BaseAspect::changed, this, &QbsBuildStep::updateState);
    connect(m_keepGoing, &BaseAspect::changed, this, &QbsBuildStep::updateState);
    connect(m_showCommandLines, &BaseAspect::changed, this, &QbsBuildStep::updateState);
    connect(m_install, &BaseAspect::changed, this, &QbsBuildStep::updateState);
    connect(m_cleanInstallDir, &BaseAspect::changed, this, &QbsBuildStep::updateState);
    connect(m_forceProbes, &BaseAspect::changed, this, &QbsBuildStep::updateState);

    connect(m_buildVariant, &SelectionAspect::changed, this, [this] {
        setBuildVariant(m_buildVariant->itemValue().toString());
    });
    connect(m_selectedAbis, &SelectionAspect::changed, [this] {
        setConfiguredArchitectures(m_selectedAbis->selectedArchitectures()); });
}

QbsBuildStep::~QbsBuildStep()
{
    doCancel();
    if (m_session)
        m_session->disconnect(this);
}

bool QbsBuildStep::init()
{
    if (m_session)
        return false;

    auto bc = static_cast<QbsBuildConfiguration *>(buildConfiguration());

    if (!bc)
        return false;

    m_changedFiles = bc->changedFiles();
    m_activeFileTags = bc->activeFileTags();
    m_products = bc->products();

    return true;
}

void QbsBuildStep::setupOutputFormatter(OutputFormatter *formatter)
{
    formatter->addLineParsers(target()->kit()->createOutputParsers());
    BuildStep::setupOutputFormatter(formatter);
}

void QbsBuildStep::doRun()
{
    // We need a pre-build parsing step in order not to lose project file changes done
    // right before building (but before the delay has elapsed).
    m_parsingAfterBuild = false;
    parseProject();
}

QWidget *QbsBuildStep::createConfigWidget()
{
    return new QbsBuildStepConfigWidget(this);
}

void QbsBuildStep::doCancel()
{
    if (m_parsingProject)
        qbsBuildSystem()->cancelParsing();
    else if (m_session)
        m_session->cancelCurrentJob();
}

QVariantMap QbsBuildStep::qbsConfiguration(VariableHandling variableHandling) const
{
    QVariantMap config = m_qbsConfiguration;
    const auto qbsBuildConfig = static_cast<QbsBuildConfiguration *>(buildConfiguration());
    config.insert(Constants::QBS_FORCE_PROBES_KEY, m_forceProbes->value());

    const auto store = [&config](TriState ts, const QString &key) {
        if (ts == TriState::Enabled)
            config.insert(key, true);
        else if (ts == TriState::Disabled)
            config.insert(key, false);
        else
            config.remove(key);
    };

    store(qbsBuildConfig->separateDebugInfoSetting(),
          Constants::QBS_CONFIG_SEPARATE_DEBUG_INFO_KEY);

    store(qbsBuildConfig->qmlDebuggingSetting(),
          Constants::QBS_CONFIG_QUICK_DEBUG_KEY);

    store(qbsBuildConfig->qtQuickCompilerSetting(),
          Constants::QBS_CONFIG_QUICK_COMPILER_KEY);

    if (variableHandling == ExpandVariables) {
        const MacroExpander * const expander = macroExpander();
        for (auto it = config.begin(), end = config.end(); it != end; ++it) {
            const QString rawString = it.value().toString();
            const QString expandedString = expander->expand(rawString);
            it.value() = expandedString;
        }
    }
    return config;
}

void QbsBuildStep::setQbsConfiguration(const QVariantMap &config)
{
    QVariantMap tmp = config;
    tmp.insert(Constants::QBS_CONFIG_PROFILE_KEY, qbsBuildSystem()->profile());
    QString buildVariant = tmp.value(Constants::QBS_CONFIG_VARIANT_KEY).toString();
    if (buildVariant.isEmpty()) {
        buildVariant = Constants::QBS_VARIANT_DEBUG;
        tmp.insert(Constants::QBS_CONFIG_VARIANT_KEY, buildVariant);
    }
    if (tmp == m_qbsConfiguration)
        return;
    m_qbsConfiguration = tmp;
    if (m_buildVariant)
        m_buildVariant->setValue(m_buildVariant->indexForItemValue(buildVariant));
    if (ProjectExplorer::BuildConfiguration *bc = buildConfiguration())
        emit bc->buildTypeChanged();
    emit qbsConfigurationChanged();
}

bool QbsBuildStep::hasCustomInstallRoot() const
{
    return m_qbsConfiguration.contains(Constants::QBS_INSTALL_ROOT_KEY);
}

Utils::FilePath QbsBuildStep::installRoot(VariableHandling variableHandling) const
{
    const QString root =
            qbsConfiguration(variableHandling).value(Constants::QBS_INSTALL_ROOT_KEY).toString();
    if (!root.isNull())
        return Utils::FilePath::fromUserInput(root);
    QString defaultInstallDir = QbsSettings::defaultInstallDirTemplate();
    if (variableHandling == VariableHandling::ExpandVariables)
        defaultInstallDir = macroExpander()->expand(defaultInstallDir);
    return FilePath::fromUserInput(defaultInstallDir);
}

int QbsBuildStep::maxJobs() const
{
    if (m_maxJobCount->value() > 0)
        return m_maxJobCount->value();
    return QThread::idealThreadCount();
}

bool QbsBuildStep::fromMap(const QVariantMap &map)
{
    if (!ProjectExplorer::BuildStep::fromMap(map))
        return false;

    setQbsConfiguration(map.value(QBS_CONFIG).toMap());
    return true;
}

QVariantMap QbsBuildStep::toMap() const
{
    QVariantMap map = ProjectExplorer::BuildStep::toMap();
    map.insert(QBS_CONFIG, m_qbsConfiguration);
    return map;
}

void QbsBuildStep::buildingDone(const ErrorInfo &error)
{
    m_session->disconnect(this);
    m_session = nullptr;
    m_lastWasSuccess = !error.hasError();
    for (const ErrorInfoItem &item : std::as_const(error.items)) {
        createTaskAndOutput(
                    ProjectExplorer::Task::Error,
                    item.description,
                    item.filePath.toString(),
                    item.line);
    }

    // Building can uncover additional target artifacts.
    qbsBuildSystem()->updateAfterBuild();

    // The reparsing, if it is necessary, has to be done before finished() is emitted, as
    // otherwise a potential additional build step could conflict with the parsing step.
    if (qbsBuildSystem()->parsingScheduled()) {
        m_parsingAfterBuild = true;
        parseProject();
    } else {
        finish();
    }
}

void QbsBuildStep::reparsingDone(bool success)
{
    disconnect(target(), &Target::parsingFinished, this, &QbsBuildStep::reparsingDone);
    m_parsingProject = false;
    if (m_parsingAfterBuild) {
        finish();
    } else if (!success) {
        m_lastWasSuccess = false;
        finish();
    } else {
        build();
    }
}

void QbsBuildStep::handleTaskStarted(const QString &desciption, int max)
{
    m_currentTask = desciption;
    m_maxProgress = max;
}

void QbsBuildStep::handleProgress(int value)
{
    if (m_maxProgress > 0)
        emit progress(value * 100 / m_maxProgress, m_currentTask);
}

void QbsBuildStep::handleCommandDescription(const QString &message)
{
    emit addOutput(message, OutputFormat::Stdout);
}

void QbsBuildStep::handleProcessResult(
        const FilePath &executable,
        const QStringList &arguments,
        const FilePath &workingDir,
        const QStringList &stdOut,
        const QStringList &stdErr,
        bool success)
{
    Q_UNUSED(workingDir);
    const bool hasOutput = !stdOut.isEmpty() || !stdErr.isEmpty();
    if (success && !hasOutput)
        return;

    emit addOutput(executable.toUserOutput() + ' '  + ProcessArgs::joinArgs(arguments),
                   OutputFormat::Stdout);
    for (const QString &line : stdErr)
        emit addOutput(line, OutputFormat::Stderr);
    for (const QString &line : stdOut)
        emit addOutput(line, OutputFormat::Stdout);
}

void QbsBuildStep::createTaskAndOutput(ProjectExplorer::Task::TaskType type, const QString &message,
                                       const QString &file, int line)
{
    emit addOutput(message, OutputFormat::Stdout);
    emit addTask(CompileTask(type, message, FilePath::fromString(file), line), 1);
}

QString QbsBuildStep::buildVariant() const
{
    return qbsConfiguration(PreserveVariables).value(Constants::QBS_CONFIG_VARIANT_KEY).toString();
}

QbsBuildSystem *QbsBuildStep::qbsBuildSystem() const
{
    return static_cast<QbsBuildSystem *>(buildSystem());
}

void QbsBuildStep::setBuildVariant(const QString &variant)
{
    if (m_qbsConfiguration.value(Constants::QBS_CONFIG_VARIANT_KEY).toString() == variant)
        return;
    m_qbsConfiguration.insert(Constants::QBS_CONFIG_VARIANT_KEY, variant);
    emit qbsConfigurationChanged();
    if (ProjectExplorer::BuildConfiguration *bc = buildConfiguration())
        emit bc->buildTypeChanged();
}

QString QbsBuildStep::profile() const
{
    return qbsConfiguration(PreserveVariables).value(Constants::QBS_CONFIG_PROFILE_KEY).toString();
}

void QbsBuildStep::parseProject()
{
    m_parsingProject = true;
    connect(target(), &Target::parsingFinished, this, &QbsBuildStep::reparsingDone);
    qbsBuildSystem()->parseCurrentBuildConfiguration();
}

void QbsBuildStep::build()
{
    m_session = qbsBuildSystem()->session();
    if (!m_session) {
        emit addOutput(QbsProjectManager::Tr::tr("No qbs session exists for this target."),
                       OutputFormat::ErrorMessage);
        emit finished(false);
        return;
    }

    QJsonObject request;
    request.insert("type", "build-project");
    request.insert("max-job-count", maxJobs());
    request.insert("keep-going", keepGoing());
    request.insert("command-echo-mode", showCommandLines() ? "command-line" : "summary");
    request.insert("install", install());
    QbsSession::insertRequestedModuleProperties(request);
    request.insert("clean-install-root", cleanInstallRoot());
    if (!m_products.isEmpty())
        request.insert("products", QJsonArray::fromStringList(m_products));
    if (!m_changedFiles.isEmpty()) {
        const auto changedFilesArray = QJsonArray::fromStringList(m_changedFiles);
        request.insert("changed-files", changedFilesArray);
        request.insert("files-to-consider", changedFilesArray);
    }
    if (!m_activeFileTags.isEmpty())
        request.insert("active-file-tags", QJsonArray::fromStringList(m_activeFileTags));
    request.insert("data-mode", "only-if-changed");

    m_session->sendRequest(request);
    m_maxProgress = 0;
    connect(m_session, &QbsSession::projectBuilt, this, &QbsBuildStep::buildingDone);
    connect(m_session, &QbsSession::taskStarted, this, &QbsBuildStep::handleTaskStarted);
    connect(m_session, &QbsSession::taskProgress, this, &QbsBuildStep::handleProgress);
    connect(m_session, &QbsSession::commandDescription,
            this, &QbsBuildStep::handleCommandDescription);
    connect(m_session, &QbsSession::processResult, this, &QbsBuildStep::handleProcessResult);
    connect(m_session, &QbsSession::errorOccurred, this, [this] {
        buildingDone(ErrorInfo(QbsProjectManager::Tr::tr("Build canceled: Qbs session failed.")));
    });
}

void QbsBuildStep::finish()
{
    m_session = nullptr;
    emit finished(m_lastWasSuccess);
}

void QbsBuildStep::updateState()
{
    emit qbsConfigurationChanged();
}

void QbsBuildStep::setConfiguredArchitectures(const QStringList &architectures)
{
    if (configuredArchitectures() == architectures)
        return;
    if (architectures.isEmpty())
        m_qbsConfiguration.remove(Constants::QBS_ARCHITECTURES);
    else
        m_qbsConfiguration.insert(Constants::QBS_ARCHITECTURES, architectures.join(','));
    emit qbsConfigurationChanged();
}

QStringList QbsBuildStep::configuredArchitectures() const
{
    return m_qbsConfiguration[Constants::QBS_ARCHITECTURES].toString().split(',',
            Qt::SkipEmptyParts);
}

QbsBuildStepData QbsBuildStep::stepData() const
{
    QbsBuildStepData data;
    data.command = "build";
    data.dryRun = false;
    data.keepGoing = m_keepGoing->value();
    data.forceProbeExecution = m_forceProbes->value();
    data.showCommandLines = m_showCommandLines->value();
    data.noInstall = !m_install->value();
    data.noBuild = false;
    data.cleanInstallRoot = m_cleanInstallDir->value();
    data.jobCount = maxJobs();
    data.installRoot = installRoot();
    return data;
}

void QbsBuildStep::dropSession()
{
    if (m_session) {
        doCancel();
        m_session->disconnect(this);
        m_session = nullptr;
    }
}


// --------------------------------------------------------------------
// QbsBuildStepConfigWidget:
// --------------------------------------------------------------------

QbsBuildStepConfigWidget::QbsBuildStepConfigWidget(QbsBuildStep *step)
    : m_qbsStep(step)
{
    connect(step, &ProjectConfiguration::displayNameChanged,
            this, &QbsBuildStepConfigWidget::updateState);
    connect(static_cast<QbsBuildConfiguration *>(step->buildConfiguration()),
            &QbsBuildConfiguration::qbsConfigurationChanged,
            this, &QbsBuildStepConfigWidget::updateState);
    connect(step, &QbsBuildStep::qbsBuildOptionsChanged,
            this, &QbsBuildStepConfigWidget::updateState);
    connect(&QbsSettings::instance(), &QbsSettings::settingsChanged,
            this, &QbsBuildStepConfigWidget::updateState);
    connect(step->buildConfiguration(), &BuildConfiguration::buildDirectoryChanged,
            this, &QbsBuildStepConfigWidget::updateState);

    setContentsMargins(0, 0, 0, 0);

    propertyEdit = new FancyLineEdit(this);

    defaultInstallDirCheckBox = new QCheckBox(this);

    installDirChooser = new PathChooser(this);
    installDirChooser->setExpectedKind(PathChooser::Directory);

    using namespace Layouting;
    Form {
        m_qbsStep->m_buildVariant, br,
        m_qbsStep->m_selectedAbis, br,
        m_qbsStep->m_maxJobCount, br,
        QbsProjectManager::Tr::tr("Properties:"), propertyEdit, br,

        QbsProjectManager::Tr::tr("Flags:"),
        m_qbsStep->m_keepGoing,
        m_qbsStep->m_showCommandLines,
        m_qbsStep->m_forceProbes, br,

        QbsProjectManager::Tr::tr("Installation flags:"),
        m_qbsStep->m_install,
        m_qbsStep->m_cleanInstallDir,
        defaultInstallDirCheckBox, br,

        QbsProjectManager::Tr::tr("Installation directory:"), installDirChooser, br,
        m_qbsStep->m_commandLine, br,
        noMargin,
    }.attachTo(this);

    propertyEdit->setToolTip(QbsProjectManager::Tr::tr("Properties to pass to the project."));
    defaultInstallDirCheckBox->setText(QbsProjectManager::Tr::tr("Use default location"));

    auto chooser = new VariableChooser(this);
    chooser->addSupportedWidget(propertyEdit);
    chooser->addSupportedWidget(installDirChooser->lineEdit());
    chooser->addMacroExpanderProvider([step] { return step->macroExpander(); });
    propertyEdit->setValidationFunction([this](FancyLineEdit *edit, QString *errorMessage) {
        return validateProperties(edit, errorMessage);
    });

    connect(defaultInstallDirCheckBox, &QCheckBox::toggled, this,
            &QbsBuildStepConfigWidget::changeUseDefaultInstallDir);

    connect(installDirChooser, &Utils::PathChooser::rawPathChanged, this,
            &QbsBuildStepConfigWidget::changeInstallDir);

    updateState();
}

void QbsBuildStepConfigWidget::updateState()
{
    if (!m_ignoreChanges.isLocked()) {
        updatePropertyEdit(m_qbsStep->qbsConfiguration(QbsBuildStep::PreserveVariables));
        installDirChooser->setFilePath(m_qbsStep->installRoot(QbsBuildStep::PreserveVariables));
        defaultInstallDirCheckBox->setChecked(!m_qbsStep->hasCustomInstallRoot());
        m_qbsStep->m_selectedAbis->setSelectedArchitectures(m_qbsStep->configuredArchitectures());
    }

    const auto qbsBuildConfig = static_cast<QbsBuildConfiguration *>(m_qbsStep->buildConfiguration());

    QString command = qbsBuildConfig->equivalentCommandLine(m_qbsStep->stepData());

    for (int i = 0; i < m_propertyCache.count(); ++i) {
        command += ' ' + m_propertyCache.at(i).name + ':' + m_propertyCache.at(i).effectiveValue;
    }

    if (m_qbsStep->m_selectedAbis->isManagedByTarget()) {
        QStringList selectedArchitectures = m_qbsStep->configuredArchitectures();
        if (!selectedArchitectures.isEmpty()) {
            command += ' ' + QLatin1String(Constants::QBS_ARCHITECTURES) + ':' +
                    selectedArchitectures.join(',');
        }
    }

    const auto addToCommand = [&command](TriState ts, const QString &key) {
        if (ts == TriState::Enabled)
            command.append(' ').append(key).append(":true");
        else if (ts == TriState::Disabled)
            command.append(' ').append(key).append(":false");
        // Do nothing for TriState::Default
    };

    addToCommand(qbsBuildConfig->separateDebugInfoSetting(),
                 Constants::QBS_CONFIG_SEPARATE_DEBUG_INFO_KEY);

    addToCommand(qbsBuildConfig->qmlDebuggingSetting(),
                 Constants::QBS_CONFIG_QUICK_DEBUG_KEY);

    addToCommand(qbsBuildConfig->qtQuickCompilerSetting(),
                 Constants::QBS_CONFIG_QUICK_COMPILER_KEY);

    m_qbsStep->m_commandLine->setValue(command);
}


void QbsBuildStepConfigWidget::updatePropertyEdit(const QVariantMap &data)
{
    QVariantMap editable = data;

    // remove data that is edited with special UIs:
    editable.remove(Constants::QBS_CONFIG_PROFILE_KEY);
    editable.remove(Constants::QBS_CONFIG_VARIANT_KEY);
    editable.remove(Constants::QBS_CONFIG_DECLARATIVE_DEBUG_KEY); // For existing .user files
    editable.remove(Constants::QBS_CONFIG_SEPARATE_DEBUG_INFO_KEY);
    editable.remove(Constants::QBS_CONFIG_QUICK_DEBUG_KEY);
    editable.remove(Constants::QBS_CONFIG_QUICK_COMPILER_KEY);
    editable.remove(Constants::QBS_FORCE_PROBES_KEY);
    editable.remove(Constants::QBS_INSTALL_ROOT_KEY);
    if (m_qbsStep->m_selectedAbis->isManagedByTarget())
        editable.remove(Constants::QBS_ARCHITECTURES);

    QStringList propertyList;
    for (QVariantMap::const_iterator i = editable.constBegin(); i != editable.constEnd(); ++i)
        propertyList.append(i.key() + ':' + i.value().toString());

    propertyEdit->setText(ProcessArgs::joinArgs(propertyList));
}

void QbsBuildStepConfigWidget::changeUseDefaultInstallDir(bool useDefault)
{
    const GuardLocker locker(m_ignoreChanges);
    QVariantMap config = m_qbsStep->qbsConfiguration(QbsBuildStep::PreserveVariables);
    installDirChooser->setEnabled(!useDefault);
    if (useDefault)
        config.remove(Constants::QBS_INSTALL_ROOT_KEY);
    else
        config.insert(Constants::QBS_INSTALL_ROOT_KEY, installDirChooser->rawFilePath().toString());
    m_qbsStep->setQbsConfiguration(config);
}

void QbsBuildStepConfigWidget::changeInstallDir()
{
    if (!m_qbsStep->hasCustomInstallRoot())
        return;
    const GuardLocker locker(m_ignoreChanges);
    QVariantMap config = m_qbsStep->qbsConfiguration(QbsBuildStep::PreserveVariables);
    config.insert(Constants::QBS_INSTALL_ROOT_KEY, installDirChooser->rawFilePath().toString());
    m_qbsStep->setQbsConfiguration(config);
}

void QbsBuildStepConfigWidget::applyCachedProperties()
{
    QVariantMap data;
    const QVariantMap tmp = m_qbsStep->qbsConfiguration(QbsBuildStep::PreserveVariables);

    // Insert values set up with special UIs:
    data.insert(Constants::QBS_CONFIG_PROFILE_KEY,
                tmp.value(Constants::QBS_CONFIG_PROFILE_KEY));
    data.insert(Constants::QBS_CONFIG_VARIANT_KEY,
                tmp.value(Constants::QBS_CONFIG_VARIANT_KEY));
    QStringList additionalSpecialKeys({Constants::QBS_CONFIG_DECLARATIVE_DEBUG_KEY,
                                             Constants::QBS_CONFIG_QUICK_DEBUG_KEY,
                                             Constants::QBS_CONFIG_QUICK_COMPILER_KEY,
                                             Constants::QBS_CONFIG_SEPARATE_DEBUG_INFO_KEY,
                                             Constants::QBS_INSTALL_ROOT_KEY});
    if (m_qbsStep->m_selectedAbis->isManagedByTarget())
        additionalSpecialKeys << Constants::QBS_ARCHITECTURES;
    for (const QString &key : std::as_const(additionalSpecialKeys)) {
        const auto it = tmp.constFind(key);
        if (it != tmp.cend())
            data.insert(key, it.value());
    }

    for (int i = 0; i < m_propertyCache.count(); ++i) {
        const Property &property = m_propertyCache.at(i);
        data.insert(property.name, property.value);
    }

    const GuardLocker locker(m_ignoreChanges);
    m_qbsStep->setQbsConfiguration(data);
}

QbsBuildStep *QbsBuildStepConfigWidget::qbsStep() const
{
    return m_qbsStep;
}

bool QbsBuildStepConfigWidget::validateProperties(Utils::FancyLineEdit *edit, QString *errorMessage)
{
    ProcessArgs::SplitError err;
    const QStringList argList = ProcessArgs::splitArgs(edit->text(), HostOsInfo::hostOs(), false, &err);
    if (err != ProcessArgs::SplitOk) {
        if (errorMessage)
            *errorMessage = QbsProjectManager::Tr::tr("Could not split properties.");
        return false;
    }

    QList<Property> properties;
    const MacroExpander * const expander = m_qbsStep->macroExpander();
    for (const QString &rawArg : argList) {
        int pos = rawArg.indexOf(':');
        if (pos > 0) {
            const QString propertyName = rawArg.left(pos);
            QStringList specialProperties{
                Constants::QBS_CONFIG_PROFILE_KEY, Constants::QBS_CONFIG_VARIANT_KEY,
                Constants::QBS_CONFIG_QUICK_DEBUG_KEY, Constants::QBS_CONFIG_QUICK_COMPILER_KEY,
                Constants::QBS_INSTALL_ROOT_KEY, Constants::QBS_CONFIG_SEPARATE_DEBUG_INFO_KEY,
            };
            if (m_qbsStep->m_selectedAbis->isManagedByTarget())
                specialProperties << Constants::QBS_ARCHITECTURES;
            if (specialProperties.contains(propertyName)) {
                if (errorMessage) {
                    *errorMessage = QbsProjectManager::Tr::tr("Property \"%1\" cannot be set here. "
                                          "Please use the dedicated UI element.").arg(propertyName);
                }
                return false;
            }
            const QString rawValue = rawArg.mid(pos + 1);
            Property property(propertyName, rawValue, expander->expand(rawValue));
            properties.append(property);
        } else {
            if (errorMessage)
                *errorMessage = QbsProjectManager::Tr::tr("No \":\" found in property definition.");
            return false;
        }
    }

    if (m_propertyCache != properties) {
        m_propertyCache = properties;
        applyCachedProperties();
    }
    return true;
}

// --------------------------------------------------------------------
// QbsBuildStepFactory:
// --------------------------------------------------------------------

QbsBuildStepFactory::QbsBuildStepFactory()
{
    registerStep<QbsBuildStep>(Constants::QBS_BUILDSTEP_ID);
    setDisplayName(QbsProjectManager::Tr::tr("Qbs Build"));
    setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_BUILD);
    setSupportedConfiguration(Constants::QBS_BC_ID);
    setSupportedProjectType(Constants::PROJECT_ID);
}

} // namespace Internal
} // namespace QbsProjectManager

#include "qbsbuildstep.moc"
