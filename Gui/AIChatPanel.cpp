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

#include "AIChatPanel.h"

CLANG_DIAG_OFF(deprecated)
CLANG_DIAG_OFF(uninitialized)
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QHBoxLayout>
#include <QVBoxLayout>
CLANG_DIAG_ON(deprecated)
CLANG_DIAG_ON(uninitialized)

#include "Engine/AppInstance.h"
#include "Engine/Node.h"
#include "Engine/Project.h"
#include "Engine/TimeLine.h"

#include "Gui/AIAgentBackend.h"
#include "Gui/AIMcpServer.h"
#include "Gui/Gui.h"
#include "Gui/GuiAppInstance.h"
#include "Gui/NodeGraph.h"
#include "Gui/NodeGui.h"

NATRON_NAMESPACE_ENTER

struct AIChatPanelPrivate
{
    AIChatPanel* _publicInterface;
    Gui* gui;

    QVBoxLayout* mainLayout;
    QLabel* statusLabel;
    QTextBrowser* transcript;
    QPlainTextEdit* input;
    QPushButton* sendButton;
    QPushButton* stopButton;
    QLabel* providerLabel;

    AIMcpServer* server;
    AIAgentBackend* backend;

    /// True between the first token of a turn and its "result" event.
    bool turnInProgress;
    /// Whether the assistant bubble for this turn has been opened.
    bool assistantBubbleOpen;

    AIChatPanelPrivate(AIChatPanel* publicInterface,
                       Gui* g)
        : _publicInterface(publicInterface)
        , gui(g)
        , mainLayout(0)
        , statusLabel(0)
        , transcript(0)
        , input(0)
        , sendButton(0)
        , stopButton(0)
        , providerLabel(0)
        , server(0)
        , backend(0)
        , turnInProgress(false)
        , assistantBubbleOpen(false)
    {
    }

    void appendHtml(const QString& html);
    void appendUserBubble(const QString& text);
    void openAssistantBubble();
    void setStatus(const QString& text);

    /// Node names, viewer and frame the user can currently see.
    QString visibleContext() const;

    static QString escape(const QString& text);
};

QString
AIChatPanelPrivate::escape(const QString& text)
{
    QString out = text;

    out.replace( QString::fromUtf8("&"), QString::fromUtf8("&amp;") );
    out.replace( QString::fromUtf8("<"), QString::fromUtf8("&lt;") );
    out.replace( QString::fromUtf8(">"), QString::fromUtf8("&gt;") );

    return out;
}

void
AIChatPanelPrivate::appendHtml(const QString& html)
{
    transcript->moveCursor(QTextCursor::End);
    transcript->insertHtml(html);
    transcript->moveCursor(QTextCursor::End);

    QScrollBar* bar = transcript->verticalScrollBar();
    if (bar) {
        bar->setValue( bar->maximum() );
    }
}

void
AIChatPanelPrivate::appendUserBubble(const QString& text)
{
    appendHtml( QString::fromUtf8("<p><b>%1</b><br/>%2</p>")
                .arg( AIChatPanel::tr("You") )
                .arg( escape(text).replace( QString::fromUtf8("\n"), QString::fromUtf8("<br/>") ) ) );
    assistantBubbleOpen = false;
}

void
AIChatPanelPrivate::openAssistantBubble()
{
    if (assistantBubbleOpen) {
        return;
    }
    appendHtml( QString::fromUtf8("<p><b>%1</b><br/>")
                .arg( backend ? backend->displayName() : AIChatPanel::tr("Assistant") ) );
    assistantBubbleOpen = true;
}

void
AIChatPanelPrivate::setStatus(const QString& text)
{
    if (statusLabel) {
        statusLabel->setText(text);
    }
}

QString
AIChatPanelPrivate::visibleContext() const
{
    // This is the advantage a terminal CLI does not have: the panel knows what
    // the artist is looking at, so "warmer here" can resolve without naming
    // anything.
    if (!gui) {
        return QString();
    }

    QStringList parts;

    NodeGraph* graph = gui->getNodeGraph();
    if (graph) {
        const std::list<NodeGuiPtr>& selected = graph->getSelectedNodes();
        QStringList names;
        for (std::list<NodeGuiPtr>::const_iterator it = selected.begin(); it != selected.end(); ++it) {
            NodePtr node = (*it) ? (*it)->getNode() : NodePtr();
            if (node) {
                names.push_back( QString::fromUtf8( node->getScriptName_mt_safe().c_str() ) );
            }
        }
        if ( !names.isEmpty() ) {
            parts.push_back( AIChatPanel::tr("Selected nodes: %1").arg( names.join( QString::fromUtf8(", ") ) ) );
        }
    }

    GuiAppInstancePtr app = gui->getApp();
    if (app) {
        TimeLinePtr timeline = app->getTimeLine();
        if (timeline) {
            parts.push_back( AIChatPanel::tr("Current frame: %1").arg( (int)timeline->currentFrame() ) );
        }
    }

    if ( parts.isEmpty() ) {
        return QString();
    }

    return QString::fromUtf8("[Natron context] ") + parts.join( QString::fromUtf8(" | ") );
}

// ---------------------------------------------------------------------------

AIChatPanel::AIChatPanel(Gui* gui)
    : QWidget(gui)
    , PanelWidget(this, gui)
    , _imp( new AIChatPanelPrivate(this, gui) )
{
    _imp->mainLayout = new QVBoxLayout(this);
    _imp->mainLayout->setContentsMargins(4, 4, 4, 4);
    _imp->mainLayout->setSpacing(4);

    _imp->statusLabel = new QLabel(this);
    _imp->statusLabel->setText( tr("Not started") );
    _imp->mainLayout->addWidget(_imp->statusLabel);

    _imp->transcript = new QTextBrowser(this);
    _imp->transcript->setOpenExternalLinks(true);
    _imp->transcript->setReadOnly(true);
    _imp->mainLayout->addWidget(_imp->transcript, 1);

    _imp->input = new QPlainTextEdit(this);
    _imp->input->setPlaceholderText( tr("Ask the assistant to build or change the comp. Enter sends, Shift+Enter adds a line.") );
    _imp->input->setMaximumHeight(90);
    _imp->input->installEventFilter(this);
    _imp->mainLayout->addWidget(_imp->input);

    QHBoxLayout* buttons = new QHBoxLayout();
    _imp->providerLabel = new QLabel(this);
    buttons->addWidget(_imp->providerLabel, 1);

    _imp->stopButton = new QPushButton(tr("Stop"), this);
    _imp->stopButton->setEnabled(false);
    buttons->addWidget(_imp->stopButton);

    _imp->sendButton = new QPushButton(tr("Send"), this);
    buttons->addWidget(_imp->sendButton);

    _imp->mainLayout->addLayout(buttons);

    QObject::connect( _imp->sendButton, SIGNAL( clicked() ), this, SLOT( onSendClicked() ) );
    QObject::connect( _imp->stopButton, SIGNAL( clicked() ), this, SLOT( onStopClicked() ) );

    _imp->backend = new ClaudeCodeBackend(this);

    // Say plainly where the conversation goes. The user is spending their own
    // subscription, and the project content leaves the machine.
    _imp->providerLabel->setText( tr("powered by %1 - your account; the conversation and project content are sent to the provider")
                                  .arg( _imp->backend->displayName() ) );

    QObject::connect( _imp->backend, SIGNAL( textChunk(QString) ),
                      this, SLOT( onBackendTextChunk(QString) ) );
    QObject::connect( _imp->backend, SIGNAL( toolCall(QString, QString) ),
                      this, SLOT( onBackendToolCall(QString, QString) ) );
    QObject::connect( _imp->backend, SIGNAL( toolResult(QString, bool) ),
                      this, SLOT( onBackendToolResult(QString, bool) ) );
    QObject::connect( _imp->backend, SIGNAL( turnFinished() ),
                      this, SLOT( onBackendTurnFinished() ) );
    QObject::connect( _imp->backend, SIGNAL( errorOccurred(QString) ),
                      this, SLOT( onBackendError(QString) ) );
    QObject::connect( _imp->backend, SIGNAL( finished() ),
                      this, SLOT( onBackendFinished() ) );
}

AIChatPanel::~AIChatPanel()
{
    if (_imp->backend) {
        _imp->backend->stop();
    }
    if (_imp->server) {
        _imp->server->stop();
    }
}

QUndoStack*
AIChatPanel::getUndoStack() const
{
    // The agent mutates the node graph, so its undo belongs on the graph's stack
    // rather than on a stack private to this panel. Ctrl+Z with the panel
    // focused then undoes the agent's last turn.
    if (!_imp->gui) {
        return 0;
    }

    NodeGraph* graph = _imp->gui->getNodeGraph();

    return graph ? graph->getUndoStack() : 0;
}

void
AIChatPanel::ensureStarted()
{
    if (!_imp->server) {
        _imp->server = new AIMcpServer(_imp->gui, this);

        // Direct connection: the veto has to be decided before the tool runs,
        // on the same call stack.
        QObject::connect( _imp->server, SIGNAL( destructiveToolRequested(QString, QString, bool*) ),
                          this, SLOT( onDestructiveToolRequested(QString, QString, bool*) ),
                          Qt::DirectConnection );

        if ( !_imp->server->start() ) {
            _imp->setStatus( tr("Could not open the local MCP port.") );

            return;
        }
    }

    if ( _imp->backend->isRunning() ) {
        return;
    }

    const QString exe = _imp->backend->findExecutable();
    if ( exe.isEmpty() ) {
        _imp->setStatus( tr("'claude' was not found. Install Claude Code, then reopen this panel.") );
        _imp->appendHtml( QString::fromUtf8("<p><i>%1</i></p>")
                          .arg( tr("Claude Code is not installed or not on your PATH. Install it, run 'claude' once in a terminal to sign in with your subscription, then reopen this panel.") ) );

        return;
    }

    // Run the agent in the project's folder so its own file tools see the comp.
    QString cwd;
    GuiAppInstancePtr app = _imp->gui ? _imp->gui->getApp() : GuiAppInstancePtr();
    if (app) {
        ProjectPtr project = app->getProject();
        if (project) {
            const QString path = project->getProjectPath();
            if ( !path.isEmpty() && QDir(path).exists() ) {
                cwd = path;
            }
        }
    }
    if ( cwd.isEmpty() ) {
        cwd = QDir::homePath();
    }

    if ( _imp->backend->start( cwd, _imp->server->url(), _imp->server->token() ) ) {
        _imp->setStatus( tr("%1 - connected").arg( _imp->backend->displayName() ) );
    }
}

void
AIChatPanel::onSendClicked()
{
    const QString text = _imp->input->toPlainText().trimmed();

    if ( text.isEmpty() ) {
        return;
    }

    ensureStarted();

    if ( !_imp->backend->isRunning() ) {
        return;
    }

    _imp->appendUserBubble(text);
    _imp->input->clear();

    // Open one undo transaction for the whole turn: every graph mutation the
    // agent makes before it finishes collapses into a single Ctrl+Z. The
    // per-tool guards inside AIMcpServer nest harmlessly under this one.
    if (_imp->server) {
        _imp->server->beginAgentTransaction( tr("assistant turn") );
    }
    _imp->turnInProgress = true;
    _imp->stopButton->setEnabled(true);
    _imp->sendButton->setEnabled(false);

    const QString context = _imp->visibleContext();
    _imp->backend->send( context.isEmpty() ? text : ( context + QString::fromUtf8("\n\n") + text ) );
}

void
AIChatPanel::onStopClicked()
{
    if (_imp->backend) {
        _imp->backend->interrupt();
    }
    onBackendTurnFinished();
}

void
AIChatPanel::onBackendTextChunk(const QString& text)
{
    _imp->openAssistantBubble();
    _imp->appendHtml( AIChatPanelPrivate::escape(text)
                      .replace( QString::fromUtf8("\n"), QString::fromUtf8("<br/>") ) );
}

void
AIChatPanel::onBackendToolCall(const QString& name,
                               const QString& argsJson)
{
    Q_UNUSED(argsJson);

    // Compact one-line activity row rather than a wall of JSON.
    QString shortName = name;
    if ( shortName.startsWith( QString::fromUtf8("mcp__natron__") ) ) {
        shortName = shortName.mid( QString::fromUtf8("mcp__natron__").size() );
    }

    _imp->assistantBubbleOpen = false;
    _imp->appendHtml( QString::fromUtf8("<p style=\"color:#888;\">&#9881; %1&#8230;</p>")
                      .arg( AIChatPanelPrivate::escape(shortName) ) );
}

void
AIChatPanel::onBackendToolResult(const QString& name,
                                 bool ok)
{
    QString shortName = name;

    if ( shortName.startsWith( QString::fromUtf8("mcp__natron__") ) ) {
        shortName = shortName.mid( QString::fromUtf8("mcp__natron__").size() );
    }

    _imp->assistantBubbleOpen = false;
    _imp->appendHtml( QString::fromUtf8("<p style=\"color:%1;\">&#9881; %2 %3</p>")
                      .arg( ok ? QString::fromUtf8("#4a4") : QString::fromUtf8("#c44") )
                      .arg( AIChatPanelPrivate::escape(shortName) )
                      .arg( ok ? QString::fromUtf8("&#10003;") : QString::fromUtf8("&#10007;") ) );
}

void
AIChatPanel::onBackendTurnFinished()
{
    if (!_imp->turnInProgress) {
        return;
    }

    _imp->turnInProgress = false;
    _imp->assistantBubbleOpen = false;

    // Close the turn's undo macro. This must happen on every exit path,
    // including errors, or the stack stays wedged open.
    if (_imp->server) {
        _imp->server->endAgentTransaction();
    }

    _imp->stopButton->setEnabled(false);
    _imp->sendButton->setEnabled(true);
}

void
AIChatPanel::onBackendError(const QString& message)
{
    _imp->assistantBubbleOpen = false;
    _imp->appendHtml( QString::fromUtf8("<p style=\"color:#c44;\"><b>%1</b> %2</p>")
                      .arg( tr("Error:") )
                      .arg( AIChatPanelPrivate::escape(message) ) );
}

void
AIChatPanel::onBackendFinished()
{
    onBackendTurnFinished();
    _imp->setStatus( tr("Agent stopped") );
}

void
AIChatPanel::onDestructiveToolRequested(const QString& toolName,
                                        const QString& summary,
                                        bool* allowed)
{
    if (!allowed) {
        return;
    }

    // Approval happens here, in Natron, with the real node names -- the agent
    // CLI exposes no permission callback, so this is the only place it can be
    // asked honestly.
    const QMessageBox::StandardButton answer =
        QMessageBox::question( this,
                               tr("AI Assistant"),
                               tr("The assistant wants to: %1\n\nAllow it?").arg(summary),
                               QMessageBox::Yes | QMessageBox::No,
                               QMessageBox::No );

    *allowed = (answer == QMessageBox::Yes);

    if (!*allowed) {
        _imp->appendHtml( QString::fromUtf8("<p style=\"color:#c44;\">%1</p>")
                          .arg( tr("Declined: %1").arg( AIChatPanelPrivate::escape(toolName) ) ) );
    }
}

bool
AIChatPanel::eventFilter(QObject* watched,
                         QEvent* event)
{
    if ( ( watched == _imp->input ) && ( event->type() == QEvent::KeyPress ) ) {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        if ( ( key->key() == Qt::Key_Return ) || ( key->key() == Qt::Key_Enter ) ) {
            if ( key->modifiers() & Qt::ShiftModifier ) {
                // Let the editor insert the newline itself.
                return false;
            }
            onSendClicked();

            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void
AIChatPanel::keyPressEvent(QKeyEvent* e)
{
    // Enter sends, Shift+Enter inserts a newline. The input is a child widget,
    // so the real handling is in the event filter; this covers the panel itself.
    if ( ( ( e->key() == Qt::Key_Return ) || ( e->key() == Qt::Key_Enter ) ) &&
         !( e->modifiers() & Qt::ShiftModifier ) ) {
        onSendClicked();
        e->accept();

        return;
    }

    handleUnCaughtKeyPressEvent(e);
    QWidget::keyPressEvent(e);
}

void
AIChatPanel::keyReleaseEvent(QKeyEvent* e)
{
    handleUnCaughtKeyUpEvent(e);
    QWidget::keyReleaseEvent(e);
}

void
AIChatPanel::focusInEvent(QFocusEvent* e)
{
    takeClickFocus();
    QWidget::focusInEvent(e);
}

void
AIChatPanel::mousePressEvent(QMouseEvent* e)
{
    takeClickFocus();
    QWidget::mousePressEvent(e);
}

void
AIChatPanel::enterEvent(QtCompat::QEnterEvent* e)
{
    enterEventBase();
    QWidget::enterEvent(e);
}

void
AIChatPanel::leaveEvent(QEvent* e)
{
    leaveEventBase();
    QWidget::leaveEvent(e);
}

NATRON_NAMESPACE_EXIT

NATRON_NAMESPACE_USING
#include "moc_AIChatPanel.cpp"
