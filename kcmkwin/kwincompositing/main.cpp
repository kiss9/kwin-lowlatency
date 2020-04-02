/**************************************************************************
 * KWin - the KDE window manager                                          *
 * This file is part of the KDE project.                                  *
 *                                                                        *
 * Copyright (C) 2013 Antonis Tsiapaliokas <kok3rs@gmail.com>             *
 * Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>                 *
 *                                                                        *
 * This program is free software; you can redistribute it and/or modify   *
 * it under the terms of the GNU General Public License as published by   *
 * the Free Software Foundation; either version 2 of the License, or      *
 * (at your option) any later version.                                    *
 *                                                                        *
 * This program is distributed in the hope that it will be useful,        *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 * GNU General Public License for more details.                           *
 *                                                                        *
 * You should have received a copy of the GNU General Public License      *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 **************************************************************************/


#include "compositing.h"
#include "ui_compositing.h"
#include <QAction>
#include <QApplication>
#include <QLayout>
#include <QSpinBox>

#include <kcmodule.h>
#include <kservice.h>

#include <algorithm>
#include <functional>

class KWinCompositingSettings : public KCModule
{
    Q_OBJECT
public:
    explicit KWinCompositingSettings(QWidget *parent = nullptr, const QVariantList &args = QVariantList());

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

private:
    void init();
    KWin::Compositing::Compositing *m_compositing;
    Ui_CompositingForm m_form;
};

static const QVector<qreal> s_animationMultipliers = {8, 4, 2, 1, 0.5, 0.25, 0.125, 0};

KWinCompositingSettings::KWinCompositingSettings(QWidget *parent, const QVariantList &args)
    : KCModule(parent, args)
    , m_compositing(new KWin::Compositing::Compositing(this))
{
    m_form.setupUi(this);
    m_form.glCrashedWarning->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    QAction *reenableGLAction = new QAction(i18n("Re-enable OpenGL detection"), this);
    connect(reenableGLAction, &QAction::triggered, m_compositing, &KWin::Compositing::Compositing::reenableOpenGLDetection);
    connect(reenableGLAction, &QAction::triggered, m_form.glCrashedWarning, &KMessageWidget::animatedHide);
    m_form.glCrashedWarning->addAction(reenableGLAction);
    m_form.scaleWarning->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    m_form.tearingWarning->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    m_form.windowThumbnailWarning->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    m_form.unredirectInformation->setIcon(QIcon::fromTheme(QStringLiteral("dialog-information")));

    m_form.compositingEnabled->setVisible(!m_compositing->compositingRequired());
    m_form.windowsBlockCompositing->setVisible(!m_compositing->compositingRequired());

    init();
}

void KWinCompositingSettings::init()
{
    using namespace KWin::Compositing;
    auto currentIndexChangedSignal = static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged);
    auto valueChangedSignal = static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged);

    connect(m_compositing, &Compositing::changed, this, &KWinCompositingSettings::markAsChanged);

    // enabled check box
    m_form.compositingEnabled->setChecked(m_compositing->compositingEnabled());
    connect(m_compositing, &Compositing::compositingEnabledChanged, m_form.compositingEnabled, &QCheckBox::setChecked);
    connect(m_form.compositingEnabled, &QCheckBox::toggled, m_compositing, &Compositing::setCompositingEnabled);

    // gl scale filter
    m_form.glScaleFilter->setCurrentIndex(m_compositing->glScaleFilter());
    connect(m_compositing, &Compositing::glScaleFilterChanged, m_form.glScaleFilter, &QComboBox::setCurrentIndex);
    connect(m_form.glScaleFilter, currentIndexChangedSignal, m_compositing, &Compositing::setGlScaleFilter);
    connect(m_form.glScaleFilter, currentIndexChangedSignal,
        [this](int index) {
            if (index == 2) {
                m_form.scaleWarning->animatedShow();
            } else {
                m_form.scaleWarning->animatedHide();
            }
        }
    );

    // xrender scale filter
    m_form.xrScaleFilter->setCurrentIndex(m_compositing->xrScaleFilter());
    connect(m_compositing, &Compositing::xrScaleFilterChanged, m_form.xrScaleFilter, &QComboBox::setCurrentIndex);
    connect(m_form.xrScaleFilter, currentIndexChangedSignal,
            [this](int index) {
        if (index == 0) {
            m_compositing->setXrScaleFilter(false);
        } else {
            m_compositing->setXrScaleFilter(true);
        }
    });

    // tearing prevention
    m_form.tearingPrevention->setCurrentIndex(m_compositing->glSwapStrategy());
    connect(m_compositing, &Compositing::glSwapStrategyChanged, m_form.tearingPrevention, &QComboBox::setCurrentIndex);
    connect(m_form.tearingPrevention, currentIndexChangedSignal, m_compositing, &Compositing::setGlSwapStrategy);
    connect(m_form.tearingPrevention, currentIndexChangedSignal,
        [this](int index) {
            if (index == 2) {
                // only when cheap - tearing
                m_form.tearingWarning->setText(i18n("\"Only when cheap\" only prevents tearing for full screen changes like a video."));
                m_form.tearingWarning->animatedShow();
            } else if (index == 3) {
                // full screen repaints
                m_form.tearingWarning->setText(i18n("\"Full screen repaints\" can cause performance problems."));
                m_form.tearingWarning->animatedShow();
            } else if (index == 4) {
                // re-use screen content
                m_form.tearingWarning->setText(i18n("\"Re-use screen content\" causes severe performance problems on MESA drivers."));
                m_form.tearingWarning->animatedShow();
            } else {
                m_form.tearingWarning->animatedHide();
            }
        }
    );

    // windowThumbnail
    m_form.windowThumbnail->setCurrentIndex(m_compositing->windowThumbnail());
    connect(m_compositing, &Compositing::windowThumbnailChanged, m_form.windowThumbnail, &QComboBox::setCurrentIndex);
    connect(m_form.windowThumbnail, currentIndexChangedSignal, m_compositing, &Compositing::setWindowThumbnail);
    connect(m_form.windowThumbnail, currentIndexChangedSignal,
        [this](int index) {
            if (index == 2) {
                m_form.windowThumbnailWarning->animatedShow();
            } else {
                m_form.windowThumbnailWarning->animatedHide();
            }
        }
    );

    // windows blocking compositing
    m_form.windowsBlockCompositing->setChecked(m_compositing->windowsBlockCompositing());
    connect(m_compositing, &Compositing::windowsBlockCompositingChanged, m_form.windowsBlockCompositing, &QCheckBox::setChecked);
    connect(m_form.windowsBlockCompositing, &QCheckBox::toggled, m_compositing, &Compositing::setWindowsBlockCompositing);

    // unredirect fullscreen
    m_form.unredirectFullscreen->setChecked(m_compositing->unredirectFullscreen());
    connect(m_compositing, &Compositing::unredirectFullscreenChanged, m_form.unredirectFullscreen, &QCheckBox::setChecked);
    connect(m_form.unredirectFullscreen, &QCheckBox::toggled, m_compositing, &Compositing::setUnredirectFullscreen);
    connect(m_form.unredirectFullscreen, &QCheckBox::toggled,
        [this](bool enabled) {
            if (enabled) {
                m_form.unredirectInformation->animatedShow();
            } else {
                m_form.unredirectInformation->animatedHide();
            }
        }
    );

    // animation curve
    m_form.animationCurve->setCurrentIndex(m_compositing->animationCurve());
    connect(m_compositing, &Compositing::animationCurveChanged, m_form.animationCurve, &QComboBox::setCurrentIndex);
    connect(m_form.animationCurve, currentIndexChangedSignal, m_compositing, &Compositing::setAnimationCurve);

    // latency control
    m_form.latencyControl->setCurrentIndex(m_compositing->latencyControl());
    connect(m_compositing, &Compositing::latencyControlChanged, m_form.latencyControl, &QComboBox::setCurrentIndex);
    connect(m_form.latencyControl, currentIndexChangedSignal, m_compositing, &Compositing::setLatencyControl);

    // max latency
    m_form.maxLatency->setValue(m_compositing->maxLatency());
    connect(m_compositing, &Compositing::maxLatencyChanged, m_form.maxLatency, &QSpinBox::setValue);
    connect(m_form.maxLatency, valueChangedSignal, m_compositing, &Compositing::setMaxLatency);

    // min latency
    m_form.minLatency->setValue(m_compositing->minLatency());
    connect(m_compositing, &Compositing::minLatencyChanged, m_form.minLatency, &QSpinBox::setValue);
    connect(m_form.minLatency, valueChangedSignal, m_compositing, &Compositing::setMinLatency);

    // vsync mechanism
    m_form.vsyncMechanism->setCurrentIndex(m_compositing->vsyncMechanism());
    connect(m_compositing, &Compositing::vsyncMechanismChanged, m_form.vsyncMechanism, &QComboBox::setCurrentIndex);
    connect(m_form.vsyncMechanism, currentIndexChangedSignal, m_compositing, &Compositing::setVsyncMechanism);

    // compositing type
    CompositingType *type = new CompositingType(this);
    m_form.type->setModel(type);
    auto updateCompositingType = [this, type]() {
        m_form.type->setCurrentIndex(type->indexForCompositingType(m_compositing->compositingType()));
    };
    updateCompositingType();
    connect(m_compositing, &Compositing::compositingTypeChanged,
        [updateCompositingType]() {
            updateCompositingType();
        }
    );
    auto showHideBasedOnType = [this, type]() {
        const int currentType = type->compositingTypeForIndex(m_form.type->currentIndex());
        m_form.glScaleFilter->setVisible(currentType != CompositingType::XRENDER_INDEX);
        m_form.glScaleFilterLabel->setVisible(currentType != CompositingType::XRENDER_INDEX);
        m_form.xrScaleFilter->setVisible(currentType == CompositingType::XRENDER_INDEX);
        m_form.xrScaleFilterLabel->setVisible(currentType == CompositingType::XRENDER_INDEX);
    };
    showHideBasedOnType();
    connect(m_form.type, currentIndexChangedSignal,
        [this, type, showHideBasedOnType]() {
            m_compositing->setCompositingType(type->compositingTypeForIndex(m_form.type->currentIndex()));
            showHideBasedOnType();
        }
    );

    if (m_compositing->OpenGLIsUnsafe()) {
        m_form.glCrashedWarning->animatedShow();
    }
}

void KWinCompositingSettings::load()
{
    KCModule::load();
    m_compositing->reset();
}

void KWinCompositingSettings::defaults()
{
    KCModule::defaults();
    m_compositing->defaults();
}

void KWinCompositingSettings::save()
{
    KCModule::save();
    m_compositing->save();
}

K_PLUGIN_FACTORY(KWinCompositingConfigFactory,
                 registerPlugin<KWinCompositingSettings>("compositing");
                )

#include "main.moc"
