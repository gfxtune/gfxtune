// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 gfxtune contributors

// Translations reach the user: a small French catalog, built from
// tests/data/translations/fr.po, is picked up by the C++ code, by plurals and
// by the window's QML, all in gfxtune's translation domain.

#include "launch/LaunchPlan.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <clocale>
#include <memory>

using namespace gfxtune;

class TranslationsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // gettext ignores the chosen language while the locale is C, as it is
        // in the build environment: any installed real locale will do.
        bool usable = false;
        for (const char *name : {"en_US.UTF-8", "en_GB.UTF-8", "fr_FR.UTF-8", "de_DE.UTF-8", "es_ES.UTF-8"}) {
            if (std::setlocale(LC_MESSAGES, name)) {
                usable = true;
                break;
            }
        }
        if (!usable) {
            QSKIP("No locale other than C is installed; gettext does not translate in the C locale.");
        }
        KLocalizedString::setApplicationDomain("gfxtune");
        KLocalizedString::addDomainLocaleDir("gfxtune", QStringLiteral(TEST_LOCALE_DIR));
        KLocalizedString::setLanguages({QStringLiteral("fr")});
    }

    void translatesWhatGfxtuneRunReports()
    {
        settings::GameSettings s;
        s.measurePerformance = false;
        s.frameCapEnabled = true;
        s.frameCap = 60;
        launch::LaunchContext context;
        context.proton = true;
        QVERIFY(launch::planLaunch(s, context).applied.contains(QStringLiteral("Limite d'images 60")));
    }

    void translatesPlurals()
    {
        QCOMPARE(i18np("1 change", "%1 changes", 1), QStringLiteral("1 modification"));
        QCOMPARE(i18np("1 change", "%1 changes", 3), QStringLiteral("3 modifications"));
    }

    void translatesTheWindow()
    {
        QQmlEngine engine;
        KLocalization::setupLocalizedContext(&engine);
        QQmlComponent component(&engine);
        component.setData("import QtQml\nimport org.kde.ki18n\nQtObject { property string text: KI18n.i18n(\"All Games\") }", QUrl());
        const std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("text").toString(), QStringLiteral("Tous les jeux"));
    }

    void leavesUntranslatedTextInEnglish() { QCOMPARE(i18n("Nothing"), QStringLiteral("Nothing")); }
};

QTEST_GUILESS_MAIN(TranslationsTest)
#include "tst_translations.moc"
