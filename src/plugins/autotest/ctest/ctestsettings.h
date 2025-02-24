// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

namespace Autotest::Internal  {

class CTestSettings : public Core::PagedSettings
{
public:
    explicit CTestSettings(Utils::Id settingsId);

    QStringList activeSettingsAsOptions() const;

    Utils::IntegerAspect repetitionCount;
    Utils::SelectionAspect repetitionMode;
    Utils::SelectionAspect outputMode;
    Utils::BoolAspect outputOnFail;
    Utils::BoolAspect stopOnFailure;
    Utils::BoolAspect scheduleRandom;
    Utils::BoolAspect repeat;
    // FIXME.. this makes the outputreader fail to get all results correctly for visual display
    Utils::BoolAspect parallel;
    Utils::IntegerAspect jobs;
    Utils::BoolAspect testLoad;
    Utils::IntegerAspect threshold;
};

} // Autotest::Internal

