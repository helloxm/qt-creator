// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "projectexplorer_export.h"

#include <coreplugin/dialogs/ioptionspage.h>

namespace ProjectExplorer {

class PROJECTEXPLORER_EXPORT BuildPropertiesSettings : public Core::PagedSettings
{
public:
    BuildPropertiesSettings();

    class BuildTriStateAspect : public Utils::TriStateAspect
    {
    public:
        BuildTriStateAspect();
    };

    Utils::StringAspect buildDirectoryTemplate;
    Utils::StringAspect buildDirectoryTemplateOld; // TODO: Remove in ~4.16
    BuildTriStateAspect separateDebugInfo;
    BuildTriStateAspect qmlDebugging;
    BuildTriStateAspect qtQuickCompiler;
    Utils::BoolAspect showQtSettings;

    void readSettings(QSettings *settings);

    QString defaultBuildDirectoryTemplate();
};

} // namespace ProjectExplorer
