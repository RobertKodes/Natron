/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <https://natrongithub.github.io/>,
 * (C) 2018-2023 The Natron developers
 * (C) 2013-2018 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "AIProviderConnectDialog.h"

#include "Gui/AIProviderRegistry.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

NATRON_NAMESPACE_ENTER

struct AIProviderConnectDialogPrivate
{
    QString providerId;
    const AIProviderInfo* info;
    AIConnectionConfig config;

    QLabel* titleLabel;
    QLabel* hintLabel;
    QLabel* cliStatusLabel;
    QPushButton* recheckCliButton;
    QPushButton* useCliButton;
    QLineEdit* apiKeyEdit;
    QLineEdit* modelEdit;
    QLineEdit* baseUrlEdit;
    QPushButton* useApiButton;
    QPushButton* useCustomButton;
    QPushButton* clearKeyButton;

    AIProviderConnectDialogPrivate()
        : providerId()
        , info(0)
        , config()
        , titleLabel(0)
        , hintLabel(0)
        , cliStatusLabel(0)
        , recheckCliButton(0)
        , useCliButton(0)
        , apiKeyEdit(0)
        , modelEdit(0)
        , baseUrlEdit(0)
        , useApiButton(0)
        , useCustomButton(0)
        , clearKeyButton(0)
    {
    }
};

AIProviderConnectDialog::AIProviderConnectDialog(const QString& providerId,
                                                 QWidget* parent)
    : QDialog(parent)
    , _imp( new AIProviderConnectDialogPrivate() )
{
    _imp->providerId = providerId;
    _imp->info = AIProviderRegistry::findById(providerId);
    _imp->config = AIConnectionSettings::loadForProvider(providerId);

    setWindowTitle( tr("Connect AI provider") );
    resize(520, 420);

    QVBoxLayout* root = new QVBoxLayout(this);

    const QString name = _imp->info ? _imp->info->displayName : providerId;
    _imp->titleLabel = new QLabel( tr("<b>%1</b>").arg(name), this );
    root->addWidget(_imp->titleLabel);

    _imp->hintLabel = new QLabel(_imp->info ? _imp->info->installHint : QString(), this);
    _imp->hintLabel->setWordWrap(true);
    root->addWidget(_imp->hintLabel);

    if (_imp->info && _imp->info->supportsCli) {
        QGroupBox* cliBox = new QGroupBox(tr("CLI (subscription login)"), this);
        QVBoxLayout* cliLayout = new QVBoxLayout(cliBox);
        _imp->cliStatusLabel = new QLabel(cliBox);
        cliLayout->addWidget(_imp->cliStatusLabel);
        QHBoxLayout* cliButtons = new QHBoxLayout();
        _imp->recheckCliButton = new QPushButton(tr("Recheck"), cliBox);
        _imp->useCliButton = new QPushButton(tr("Use CLI"), cliBox);
        cliButtons->addWidget(_imp->recheckCliButton);
        cliButtons->addWidget(_imp->useCliButton);
        cliButtons->addStretch(1);
        cliLayout->addLayout(cliButtons);
        root->addWidget(cliBox);

        connect( _imp->recheckCliButton, SIGNAL( clicked() ), this, SLOT( onRecheckCli() ) );
        connect( _imp->useCliButton, SIGNAL( clicked() ), this, SLOT( onUseCli() ) );
    }

    QGroupBox* apiBox = new QGroupBox(tr("API key"), this);
    QFormLayout* apiForm = new QFormLayout(apiBox);
    _imp->apiKeyEdit = new QLineEdit(apiBox);
    _imp->apiKeyEdit->setEchoMode(QLineEdit::Password);
    _imp->apiKeyEdit->setText(_imp->config.apiKey);
    _imp->apiKeyEdit->setPlaceholderText(tr("Paste API key (stored only on this machine)"));
    apiForm->addRow(tr("API key"), _imp->apiKeyEdit);

    _imp->modelEdit = new QLineEdit(apiBox);
    _imp->modelEdit->setText(_imp->config.model);
    apiForm->addRow(tr("Model"), _imp->modelEdit);

    _imp->baseUrlEdit = new QLineEdit(apiBox);
    _imp->baseUrlEdit->setText(_imp->config.baseUrl);
    apiForm->addRow(tr("Base URL"), _imp->baseUrlEdit);

    QHBoxLayout* apiButtons = new QHBoxLayout();
    _imp->useApiButton = new QPushButton(tr("Use API key"), apiBox);
    _imp->clearKeyButton = new QPushButton(tr("Clear saved key"), apiBox);
    apiButtons->addWidget(_imp->useApiButton);
    apiButtons->addWidget(_imp->clearKeyButton);
    apiButtons->addStretch(1);
    apiForm->addRow(apiButtons);

    if (_imp->info && _imp->info->supportsCustomEndpoint) {
        _imp->useCustomButton = new QPushButton(tr("Use custom endpoint"), apiBox);
        apiButtons->addWidget(_imp->useCustomButton);
        connect( _imp->useCustomButton, SIGNAL( clicked() ), this, SLOT( onUseCustom() ) );
    }

    root->addWidget(apiBox);

    connect( _imp->useApiButton, SIGNAL( clicked() ), this, SLOT( onUseApiKey() ) );
    connect( _imp->clearKeyButton, SIGNAL( clicked() ), this, SLOT( onClearKey() ) );

    QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect( box, SIGNAL( rejected() ), this, SLOT( reject() ) );
    root->addWidget(box);

    refreshCliStatus();
}

AIProviderConnectDialog::~AIProviderConnectDialog()
{
}

AIConnectionConfig
AIProviderConnectDialog::resultConfig() const
{
    return _imp->config;
}

void
AIProviderConnectDialog::refreshCliStatus()
{
    if (!_imp->cliStatusLabel || !_imp->info) {
        return;
    }

    const QString exe = AIProviderRegistry::findCliExecutable(_imp->info->cliBinaryName);
    if ( exe.isEmpty() ) {
        _imp->cliStatusLabel->setText( QString::fromUtf8("<span style=\"color:#c44;\">%1</span><br/><i>%2</i>")
                                       .arg( tr("CLI not found on PATH.") )
                                       .arg( _imp->info->installHint ) );
        if (_imp->useCliButton) {
            _imp->useCliButton->setEnabled(false);
        }
    } else {
        _imp->cliStatusLabel->setText( QString::fromUtf8("<span style=\"color:#4a4;\">%1</span><br/><code>%2</code>")
                                       .arg( tr("CLI found.") )
                                       .arg(exe) );
        if (_imp->useCliButton) {
            _imp->useCliButton->setEnabled(true);
        }
    }
}

void
AIProviderConnectDialog::onRecheckCli()
{
    refreshCliStatus();
}

void
AIProviderConnectDialog::onUseCli()
{
    _imp->config.providerId = _imp->providerId;
    _imp->config.method = eAIConnectionMethodCli;
    _imp->config.apiKey = _imp->apiKeyEdit->text().trimmed();
    _imp->config.model = _imp->modelEdit->text().trimmed();
    _imp->config.baseUrl = _imp->baseUrlEdit->text().trimmed();
    _imp->config.autoConnect = true;
    accept();
}

void
AIProviderConnectDialog::onUseApiKey()
{
    _imp->config.providerId = _imp->providerId;
    _imp->config.method = eAIConnectionMethodApiKey;
    _imp->config.apiKey = _imp->apiKeyEdit->text().trimmed();
    _imp->config.model = _imp->modelEdit->text().trimmed();
    _imp->config.baseUrl = _imp->baseUrlEdit->text().trimmed();
    _imp->config.autoConnect = true;

    const bool keyOptional = ( _imp->providerId == QString::fromUtf8("ollama") ) ||
                             ( _imp->providerId == QString::fromUtf8("custom") );
    if ( !keyOptional && _imp->config.apiKey.isEmpty() ) {
        _imp->hintLabel->setText( tr("Paste an API key first.") );

        return;
    }

    accept();
}

void
AIProviderConnectDialog::onUseCustom()
{
    _imp->config.providerId = _imp->providerId;
    _imp->config.method = eAIConnectionMethodCustom;
    _imp->config.apiKey = _imp->apiKeyEdit->text().trimmed();
    _imp->config.model = _imp->modelEdit->text().trimmed();
    _imp->config.baseUrl = _imp->baseUrlEdit->text().trimmed();
    _imp->config.autoConnect = true;
    if ( _imp->config.baseUrl.isEmpty() ) {
        _imp->hintLabel->setText( tr("Base URL is required for a custom endpoint.") );

        return;
    }
    accept();
}

void
AIProviderConnectDialog::onClearKey()
{
    _imp->apiKeyEdit->clear();
    AIConnectionSettings::clearApiKey(_imp->providerId);
    _imp->config.apiKey.clear();
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_AIProviderConnectDialog.cpp"
