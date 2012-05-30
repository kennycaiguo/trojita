/* Copyright (C) 2006 - 2011 Jan Kundrát <jkt@gentoo.org>

   This file is part of the Trojita Qt IMAP e-mail client,
   http://trojita.flaska.net/

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or the version 3 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include <QAuthenticator>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QScrollArea>
#include <QSplitter>
#include <QSslError>
#include <QStatusBar>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

#include "AutoCompletion.h"
#include "Window.h"
#include "ComposeWidget.h"
#include "Util.h"
#include "IconLoader.h"
#include "ProtocolLoggerWidget.h"
#include "MessageView.h"
#include "MsgListView.h"
#include "SettingsDialog.h"
#include "TaskProgressIndicator.h"
#include "Common/PortNumbers.h"
#include "Common/SettingsNames.h"
#include "SimplePartWidget.h"
#include "Imap/Model/Model.h"
#include "Imap/Model/MailboxModel.h"
#include "Imap/Model/MailboxTree.h"
#include "Imap/Model/ModelWatcher.h"
#include "Imap/Model/MsgListModel.h"
#include "Imap/Model/CombinedCache.h"
#include "Imap/Model/MemoryCache.h"
#include "Imap/Model/PrettyMailboxModel.h"
#include "Imap/Model/PrettyMsgListModel.h"
#include "Imap/Model/ThreadingMsgListModel.h"
#include "Imap/Model/Utils.h"
#include "Imap/Network/FileDownloadManager.h"
#include "Streams/SocketFactory.h"

#include "ui_CreateMailboxDialog.h"

#include "Imap/Model/ModelTest/modeltest.h"

/** @short All user-facing widgets and related classes */
namespace Gui
{

MainWindow::MainWindow(): QMainWindow(), model(0), m_ignoreStoredPassword(false)
{
    qRegisterMetaType<QList<QSslCertificate> >("QList<QSslCertificate>");
    createWidgets();

    QSettings s;
    if (! s.contains(Common::SettingsNames::imapMethodKey)) {
        QTimer::singleShot(0, this, SLOT(slotShowSettings()));
    }

    setupModels();
    createActions();
    createMenus();

    // Please note that Qt 4.6.1 really requires passing the method signature this way, *not* using the SLOT() macro
    QDesktopServices::setUrlHandler(QLatin1String("mailto"), this, "slotComposeMailUrl");

    slotUpdateWindowTitle();
}

void MainWindow::createActions()
{
    m_mainToolbar = addToolBar(tr("Navigation"));

    reloadMboxList = new QAction(style()->standardIcon(QStyle::SP_ArrowRight), tr("Update List of Child Mailboxes"), this);
    connect(reloadMboxList, SIGNAL(triggered()), this, SLOT(slotReloadMboxList()));

    resyncMbox = new QAction(loadIcon(QLatin1String("view-refresh")), tr("Check for New Messages"), this);
    connect(resyncMbox, SIGNAL(triggered()), this, SLOT(slotResyncMbox()));

    reloadAllMailboxes = new QAction(tr("Reload Everything"), this);
    // connect later

    exitAction = new QAction(loadIcon(QLatin1String("application-exit")), tr("E&xit"), this);
    exitAction->setShortcut(tr("Ctrl+Q"));
    exitAction->setStatusTip(tr("Exit the application"));
    connect(exitAction, SIGNAL(triggered()), this, SLOT(close()));

    QActionGroup *netPolicyGroup = new QActionGroup(this);
    netPolicyGroup->setExclusive(true);
    netOffline = new QAction(loadIcon(QLatin1String("network-offline")), tr("Offline"), netPolicyGroup);
    netOffline->setCheckable(true);
    // connect later
    netExpensive = new QAction(loadIcon(QLatin1String("network-expensive")), tr("Expensive Connection"), netPolicyGroup);
    netExpensive->setCheckable(true);
    // connect later
    netOnline = new QAction(loadIcon(QLatin1String("network-online")), tr("Free Access"), netPolicyGroup);
    netOnline->setCheckable(true);
    // connect later

    showFullView = new QAction(loadIcon(QLatin1String("edit-find-mail")), tr("Show Full Tree Window"), this);
    showFullView->setCheckable(true);
    connect(showFullView, SIGNAL(triggered(bool)), allDock, SLOT(setVisible(bool)));
    connect(allDock, SIGNAL(visibilityChanged(bool)), showFullView, SLOT(setChecked(bool)));

    showTaskView = new QAction(tr("Show ImapTask tree"), this);
    showTaskView->setCheckable(true);
    connect(showTaskView, SIGNAL(triggered(bool)), taskDock, SLOT(setVisible(bool)));
    connect(taskDock, SIGNAL(visibilityChanged(bool)), showTaskView, SLOT(setChecked(bool)));

    showImapLogger = new QAction(tr("Show IMAP protocol log"), this);
    showImapLogger->setCheckable(true);
    connect(showImapLogger, SIGNAL(toggled(bool)), imapLoggerDock, SLOT(setVisible(bool)));
    connect(imapLoggerDock, SIGNAL(visibilityChanged(bool)), showImapLogger, SLOT(setChecked(bool)));

    logPersistent = new QAction(tr("Log into %1").arg(Imap::Mailbox::persistentLogFileName()), this);
    logPersistent->setCheckable(true);
    connect(logPersistent, SIGNAL(triggered(bool)), imapLogger, SLOT(slotSetPersistentLogging(bool)));

    showImapCapabilities = new QAction(tr("IMAP Server Information..."), this);
    connect(showImapCapabilities, SIGNAL(triggered()), this, SLOT(slotShowImapInfo()));

    showMenuBar = new QAction(loadIcon(QLatin1String("view-list-text")),  tr("Show Main Menu Bar"), this);
    showMenuBar->setCheckable(true);
    showMenuBar->setChecked(true);
    showMenuBar->setShortcut(tr("Ctrl+M"));
    addAction(showMenuBar);   // otherwise it won't work with hidden menu bar
    connect(showMenuBar, SIGNAL(triggered(bool)), menuBar(), SLOT(setVisible(bool)));

    showToolBar = new QAction(tr("Show Toolbar"), this);
    showToolBar->setCheckable(true);
    showToolBar->setChecked(true);
    connect(showToolBar, SIGNAL(triggered(bool)), m_mainToolbar, SLOT(setVisible(bool)));

    configSettings = new QAction(loadIcon(QLatin1String("configure")),  tr("Settings..."), this);
    connect(configSettings, SIGNAL(triggered()), this, SLOT(slotShowSettings()));

    composeMail = new QAction(loadIcon(QLatin1String("document-edit")),  tr("Compose Mail..."), this);
    connect(composeMail, SIGNAL(triggered()), this, SLOT(slotComposeMail()));

    expunge = new QAction(loadIcon(QLatin1String("trash-empty")),  tr("Expunge Mailbox"), this);
    expunge->setShortcut(tr("Ctrl+E"));
    connect(expunge, SIGNAL(triggered()), this, SLOT(slotExpunge()));

    markAsRead = new QAction(loadIcon(QLatin1String("mail-mark-read")),  tr("Mark as Read"), this);
    markAsRead->setCheckable(true);
    markAsRead->setShortcut(Qt::Key_M);
    msgListTree->addAction(markAsRead);
    connect(markAsRead, SIGNAL(triggered(bool)), this, SLOT(handleMarkAsRead(bool)));

    m_nextMessage = new QAction(tr("Next Unread Message"), this);
    m_nextMessage->setShortcut(Qt::Key_N);
    msgListTree->addAction(m_nextMessage);
    msgView->addAction(m_nextMessage);
    connect(m_nextMessage, SIGNAL(triggered()), this, SLOT(slotNextUnread()));

    m_previousMessage = new QAction(tr("Previous Unread Message"), this);
    m_previousMessage->setShortcut(Qt::Key_P);
    msgListTree->addAction(m_previousMessage);
    msgView->addAction(m_previousMessage);
    connect(m_previousMessage, SIGNAL(triggered()), this, SLOT(slotPreviousUnread()));

    markAsDeleted = new QAction(loadIcon(QLatin1String("list-remove")),  tr("Mark as Deleted"), this);
    markAsDeleted->setCheckable(true);
    markAsDeleted->setShortcut(Qt::Key_Delete);
    msgListTree->addAction(markAsDeleted);
    connect(markAsDeleted, SIGNAL(triggered(bool)), this, SLOT(handleMarkAsDeleted(bool)));

    saveWholeMessage = new QAction(loadIcon(QLatin1String("file-save")), tr("Save Message..."), this);
    msgListTree->addAction(saveWholeMessage);
    connect(saveWholeMessage, SIGNAL(triggered()), this, SLOT(slotSaveCurrentMessageBody()));

    viewMsgHeaders = new QAction(tr("View Message Headers..."), this);
    msgListTree->addAction(viewMsgHeaders);
    connect(viewMsgHeaders, SIGNAL(triggered()), this, SLOT(slotViewMsgHeaders()));

    createChildMailbox = new QAction(tr("Create Child Mailbox..."), this);
    connect(createChildMailbox, SIGNAL(triggered()), this, SLOT(slotCreateMailboxBelowCurrent()));

    createTopMailbox = new QAction(tr("Create New Mailbox..."), this);
    connect(createTopMailbox, SIGNAL(triggered()), this, SLOT(slotCreateTopMailbox()));

    deleteCurrentMailbox = new QAction(tr("Delete Mailbox"), this);
    connect(deleteCurrentMailbox, SIGNAL(triggered()), this, SLOT(slotDeleteCurrentMailbox()));

#ifdef XTUPLE_CONNECT
    xtIncludeMailboxInSync = new QAction(tr("Synchronize with xTuple"), this);
    xtIncludeMailboxInSync->setCheckable(true);
    connect(xtIncludeMailboxInSync, SIGNAL(triggered()), this, SLOT(slotXtSyncCurrentMailbox()));
#endif

    releaseMessageData = new QAction(tr("Release memory for this message"), this);
    connect(releaseMessageData, SIGNAL(triggered()), this, SLOT(slotReleaseSelectedMessage()));

    replyTo = new QAction(tr("Reply..."), this);
    replyTo->setShortcut(tr("Ctrl+R"));
    connect(replyTo, SIGNAL(triggered()), this, SLOT(slotReplyTo()));

    replyAll = new QAction(tr("Reply All..."), this);
    replyAll->setShortcut(tr("Ctrl+Shift+R"));
    connect(replyAll, SIGNAL(triggered()), this, SLOT(slotReplyAll()));

    actionThreadMsgList = new QAction(tr("Show Messages in Threads"), this);
    actionThreadMsgList->setCheckable(true);
    // This action is enabled/disabled by model's capabilities
    actionThreadMsgList->setEnabled(false);
    if (QSettings().value(Common::SettingsNames::guiMsgListShowThreading).toBool()) {
        actionThreadMsgList->setChecked(true);
        // The actual threading will be performed only when model updates its capabilities
    }
    connect(actionThreadMsgList, SIGNAL(triggered(bool)), this, SLOT(slotThreadMsgList()));

    actionHideRead = new QAction(tr("Hide Read Messages"), this);
    actionHideRead->setCheckable(true);
    addAction(actionHideRead);
    if (QSettings().value(Common::SettingsNames::guiMsgListHideRead).toBool()) {
        actionHideRead->setChecked(true);
        prettyMsgListModel->setHideRead(true);
    }
    connect(actionHideRead, SIGNAL(triggered(bool)), this, SLOT(slotHideRead()));

    aboutTrojita = new QAction(trUtf8("About Trojitá..."), this);
    connect(aboutTrojita, SIGNAL(triggered()), this, SLOT(slotShowAboutTrojita()));

    donateToTrojita = new QAction(tr("Donate to the project"), this);
    connect(donateToTrojita, SIGNAL(triggered()), this, SLOT(slotDonateToTrojita()));

    connectModelActions();

    m_mainToolbar->addAction(composeMail);
    m_mainToolbar->addAction(replyTo);
    m_mainToolbar->addAction(replyAll);
    m_mainToolbar->addAction(expunge);
    m_mainToolbar->addSeparator();
    m_mainToolbar->addAction(markAsRead);
    m_mainToolbar->addAction(markAsDeleted);
    m_mainToolbar->addSeparator();
    m_mainToolbar->addAction(showMenuBar);
    m_mainToolbar->addAction(configSettings);
}

void MainWindow::connectModelActions()
{
    connect(reloadAllMailboxes, SIGNAL(triggered()), model, SLOT(reloadMailboxList()));
    connect(netOffline, SIGNAL(triggered()), model, SLOT(setNetworkOffline()));
    connect(netExpensive, SIGNAL(triggered()), model, SLOT(setNetworkExpensive()));
    connect(netOnline, SIGNAL(triggered()), model, SLOT(setNetworkOnline()));
}

void MainWindow::createMenus()
{
    QMenu *imapMenu = menuBar()->addMenu(tr("IMAP"));
    imapMenu->addAction(composeMail);
    imapMenu->addAction(replyTo);
    imapMenu->addAction(replyAll);
    imapMenu->addAction(expunge);
    imapMenu->addSeparator()->setText(tr("Network Access"));
    QMenu *netPolicyMenu = imapMenu->addMenu(tr("Network Access"));
    netPolicyMenu->addAction(netOffline);
    netPolicyMenu->addAction(netExpensive);
    netPolicyMenu->addAction(netOnline);
    QMenu *debugMenu = imapMenu->addMenu(tr("Debugging"));
    debugMenu->addAction(showFullView);
    debugMenu->addAction(showTaskView);
    debugMenu->addAction(showImapLogger);
    debugMenu->addAction(logPersistent);
    debugMenu->addAction(showImapCapabilities);
    imapMenu->addSeparator();
    imapMenu->addAction(configSettings);
    imapMenu->addSeparator();
    imapMenu->addAction(exitAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("View"));
    viewMenu->addAction(showMenuBar);
    viewMenu->addAction(showToolBar);
    viewMenu->addSeparator();
    viewMenu->addAction(m_previousMessage);
    viewMenu->addAction(m_nextMessage);
    viewMenu->addSeparator();
    viewMenu->addAction(actionThreadMsgList);
    viewMenu->addAction(actionHideRead);

    QMenu *mailboxMenu = menuBar()->addMenu(tr("Mailbox"));
    mailboxMenu->addAction(resyncMbox);
    mailboxMenu->addSeparator();
    mailboxMenu->addAction(reloadAllMailboxes);

    QMenu *helpMenu = menuBar()->addMenu(tr("Help"));
    helpMenu->addAction(donateToTrojita);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutTrojita);

    networkIndicator->setMenu(netPolicyMenu);
    networkIndicator->setDefaultAction(netOnline);
}

void MainWindow::createWidgets()
{
    mboxTree = new QTreeView();
    mboxTree->setUniformRowHeights(true);
    mboxTree->setContextMenuPolicy(Qt::CustomContextMenu);
    mboxTree->setSelectionMode(QAbstractItemView::SingleSelection);
    mboxTree->setAllColumnsShowFocus(true);
    mboxTree->setAcceptDrops(true);
    mboxTree->setDropIndicatorShown(true);
    mboxTree->setHeaderHidden(true);
    connect(mboxTree, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(showContextMenuMboxTree(const QPoint &)));

    msgListTree = new MsgListView();
    msgListTree->setUniformRowHeights(true);
    msgListTree->setContextMenuPolicy(Qt::CustomContextMenu);
    msgListTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    msgListTree->setAllColumnsShowFocus(true);
    msgListTree->setAlternatingRowColors(true);
    msgListTree->setDragEnabled(true);
    msgListTree->setRootIsDecorated(false);

    connect(msgListTree, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(showContextMenuMsgListTree(const QPoint &)));
    connect(msgListTree, SIGNAL(activated(const QModelIndex &)), this, SLOT(msgListActivated(const QModelIndex &)));
    connect(msgListTree, SIGNAL(clicked(const QModelIndex &)), this, SLOT(msgListClicked(const QModelIndex &)));
    connect(msgListTree, SIGNAL(doubleClicked(const QModelIndex &)), this, SLOT(msgListDoubleClicked(const QModelIndex &)));

    msgView = new MessageView();
    area = new QScrollArea();
    area->setWidget(msgView);
    area->setWidgetResizable(true);
    connect(msgView, SIGNAL(messageChanged()), this, SLOT(scrollMessageUp()));
    if (QSettings().value(Common::SettingsNames::appLoadHomepage, QVariant(true)).toBool() &&
        !QSettings().value(Common::SettingsNames::imapStartOffline).toBool()) {
        msgView->setHomepageUrl(QUrl(QString::fromAscii("http://welcome.trojita.flaska.net/%1").arg(QCoreApplication::applicationVersion())));
    }

    QSplitter *hSplitter = new QSplitter();
    QSplitter *vSplitter = new QSplitter();
    vSplitter->setOrientation(Qt::Vertical);
    vSplitter->addWidget(msgListTree);
    vSplitter->addWidget(area);
    hSplitter->addWidget(mboxTree);
    hSplitter->addWidget(vSplitter);

    // The mboxTree shall not expand...
    hSplitter->setStretchFactor(0, 0);
    // ...while the msgListTree shall consume all the remaining space
    hSplitter->setStretchFactor(1, 1);

    setCentralWidget(hSplitter);

    allDock = new QDockWidget("Everything", this);
    allTree = new QTreeView(allDock);
    allDock->hide();
    allTree->setUniformRowHeights(true);
    allTree->setHeaderHidden(true);
    allDock->setWidget(allTree);
    addDockWidget(Qt::LeftDockWidgetArea, allDock);
    taskDock = new QDockWidget("IMAP Tasks", this);
    taskTree = new QTreeView(taskDock);
    taskDock->hide();
    taskTree->setHeaderHidden(true);
    taskDock->setWidget(taskTree);
    addDockWidget(Qt::LeftDockWidgetArea, taskDock);

    imapLoggerDock = new QDockWidget(tr("IMAP Protocol"), this);
    imapLogger = new ProtocolLoggerWidget(imapLoggerDock);
    imapLoggerDock->hide();
    imapLoggerDock->setWidget(imapLogger);
    addDockWidget(Qt::BottomDockWidgetArea, imapLoggerDock);

    busyParsersIndicator = new TaskProgressIndicator(this);
    statusBar()->addPermanentWidget(busyParsersIndicator);
    busyParsersIndicator->hide();

    networkIndicator = new QToolButton(this);
    networkIndicator->setPopupMode(QToolButton::InstantPopup);
    statusBar()->addPermanentWidget(networkIndicator);
}

void MainWindow::setupModels()
{
    Imap::Mailbox::SocketFactoryPtr factory;
    Imap::Mailbox::TaskFactoryPtr taskFactory(new Imap::Mailbox::TaskFactory());
    QSettings s;

    using Common::SettingsNames;
    if (s.value(SettingsNames::imapMethodKey).toString() == SettingsNames::methodTCP) {
        factory.reset(new Imap::Mailbox::TlsAbleSocketFactory(
                          s.value(SettingsNames::imapHostKey).toString(),
                          s.value(SettingsNames::imapPortKey, QString::number(Common::PORT_IMAP)).toUInt()));
        factory->setStartTlsRequired(s.value(SettingsNames::imapStartTlsKey, true).toBool());
    } else if (s.value(SettingsNames::imapMethodKey).toString() == SettingsNames::methodSSL) {
        factory.reset(new Imap::Mailbox::SslSocketFactory(
                          s.value(SettingsNames::imapHostKey).toString(),
                          s.value(SettingsNames::imapPortKey, QString::number(Common::PORT_IMAPS)).toUInt()));
    } else {
        QStringList args = s.value(SettingsNames::imapProcessKey).toString().split(QLatin1Char(' '));
        if (args.isEmpty()) {
            // it's going to fail anyway
            args << QLatin1String("");
        }
        QString appName = args.takeFirst();
        factory.reset(new Imap::Mailbox::ProcessSocketFactory(appName, args));
    }

    QString cacheDir = QDesktopServices::storageLocation(QDesktopServices::CacheLocation);
    if (cacheDir.isEmpty())
        cacheDir = QDir::homePath() + QLatin1String("/.") + QCoreApplication::applicationName();
    Imap::Mailbox::AbstractCache *cache = 0;

    bool shouldUsePersistentCache = s.value(SettingsNames::cacheMetadataKey).toString() == SettingsNames::cacheMetadataPersistent;

    if (shouldUsePersistentCache) {
        if (! QDir().mkpath(cacheDir)) {
            QMessageBox::critical(this, tr("Cache Error"), tr("Failed to create directory %1").arg(cacheDir));
            shouldUsePersistentCache = false;
        }
    }

    //setProperty( "trojita-sqlcache-commit-period", QVariant(5000) );
    //setProperty( "trojita-sqlcache-commit-delay", QVariant(1000) );

    if (! shouldUsePersistentCache) {
        cache = new Imap::Mailbox::MemoryCache(this, QString());
    } else {
        cache = new Imap::Mailbox::CombinedCache(this, QLatin1String("trojita-imap-cache"), cacheDir);
        connect(cache, SIGNAL(error(QString)), this, SLOT(cacheError(QString)));
        if (! static_cast<Imap::Mailbox::CombinedCache *>(cache)->open()) {
            // Error message was already shown by the cacheError() slot
            cache->deleteLater();
            cache = new Imap::Mailbox::MemoryCache(this, QString());
        }
    }
    model = new Imap::Mailbox::Model(this, cache, factory, taskFactory, s.value(SettingsNames::imapStartOffline).toBool());
    model->setObjectName(QLatin1String("model"));
    if (s.value(SettingsNames::imapEnableId, true).toBool()) {
        model->setProperty("trojita-imap-enable-id", true);
    }
    mboxModel = new Imap::Mailbox::MailboxModel(this, model);
    mboxModel->setObjectName(QLatin1String("mboxModel"));
    prettyMboxModel = new Imap::Mailbox::PrettyMailboxModel(this, mboxModel);
    prettyMboxModel->setObjectName(QLatin1String("prettyMboxModel"));
    msgListModel = new Imap::Mailbox::MsgListModel(this, model);
    msgListModel->setObjectName(QLatin1String("msgListModel"));
    threadingMsgListModel = new Imap::Mailbox::ThreadingMsgListModel(this);
    // no call to setSourceModel() at this point
    threadingMsgListModel->setObjectName(QLatin1String("threadingMsgListModel"));
    prettyMsgListModel = new Imap::Mailbox::PrettyMsgListModel(this);
    prettyMsgListModel->setSourceModel(msgListModel);
    prettyMsgListModel->setObjectName(QLatin1String("prettyMsgListModel"));

    connect(mboxTree, SIGNAL(clicked(const QModelIndex &)), msgListModel, SLOT(setMailbox(const QModelIndex &)));
    connect(mboxTree, SIGNAL(activated(const QModelIndex &)), msgListModel, SLOT(setMailbox(const QModelIndex &)));
    connect(msgListModel, SIGNAL(mailboxChanged()), this, SLOT(slotResizeMsgListColumns()));
    connect(msgListModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(updateMessageFlags()));
    connect(msgListModel, SIGNAL(messagesAvailable()), msgListTree, SLOT(scrollToBottom()));

    connect(model, SIGNAL(alertReceived(const QString &)), this, SLOT(alertReceived(const QString &)));
    connect(model, SIGNAL(connectionError(const QString &)), this, SLOT(connectionError(const QString &)));
    connect(model, SIGNAL(authRequested()), this, SLOT(authenticationRequested()), Qt::QueuedConnection);
    connect(model, SIGNAL(authAttemptFailed(QString)), this, SLOT(authenticationFailed(QString)));
    connect(model, SIGNAL(needsSslDecision(QList<QSslCertificate>,QList<QSslError>)),
            this, SLOT(sslErrors(QList<QSslCertificate>,QList<QSslError>)), Qt::QueuedConnection);

    connect(model, SIGNAL(networkPolicyOffline()), this, SLOT(networkPolicyOffline()));
    connect(model, SIGNAL(networkPolicyExpensive()), this, SLOT(networkPolicyExpensive()));
    connect(model, SIGNAL(networkPolicyOnline()), this, SLOT(networkPolicyOnline()));

    connect(model, SIGNAL(connectionStateChanged(QObject *,Imap::ConnectionState)),
            this, SLOT(showConnectionStatus(QObject *,Imap::ConnectionState)));

    connect(model, SIGNAL(mailboxDeletionFailed(QString,QString)), this, SLOT(slotMailboxDeleteFailed(QString,QString)));
    connect(model, SIGNAL(mailboxCreationFailed(QString,QString)), this, SLOT(slotMailboxCreateFailed(QString,QString)));

    connect(model, SIGNAL(logged(uint,Imap::Mailbox::LogMessage)), imapLogger, SLOT(slotImapLogged(uint,Imap::Mailbox::LogMessage)));

    connect(model, SIGNAL(mailboxFirstUnseenMessage(QModelIndex,QModelIndex)), this, SLOT(slotScrollToUnseenMessage(QModelIndex,QModelIndex)));

    connect(model, SIGNAL(capabilitiesUpdated(QStringList)), this, SLOT(slotCapabilitiesUpdated(QStringList)));

    connect(msgListModel, SIGNAL(modelReset()), this, SLOT(slotUpdateWindowTitle()));
    connect(model, SIGNAL(messageCountPossiblyChanged(QModelIndex)), this, SLOT(slotUpdateWindowTitle()));

    //Imap::Mailbox::ModelWatcher* w = new Imap::Mailbox::ModelWatcher( this );
    //w->setModel( model );

    //ModelTest* tester = new ModelTest( prettyMboxModel, this ); // when testing, test just one model at time

    mboxTree->setModel(prettyMboxModel);
    msgListTree->setModel(prettyMsgListModel);
    connect(msgListTree->selectionModel(), SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
            this, SLOT(msgListSelectionChanged(const QItemSelection &, const QItemSelection &)));

    allTree->setModel(model);
    taskTree->setModel(model->taskModel());
    connect(model->taskModel(), SIGNAL(layoutChanged()), taskTree, SLOT(expandAll()));
    connect(model->taskModel(), SIGNAL(modelReset()), taskTree, SLOT(expandAll()));
    connect(model->taskModel(), SIGNAL(rowsInserted(QModelIndex,int,int)), taskTree, SLOT(expandAll()));
    connect(model->taskModel(), SIGNAL(rowsRemoved(QModelIndex,int,int)), taskTree, SLOT(expandAll()));
    connect(model->taskModel(), SIGNAL(rowsMoved(QModelIndex,int,int,QModelIndex,int)), taskTree, SLOT(expandAll()));

    busyParsersIndicator->setImapModel(model);

    autoCompletionModel = new AutoCompletionModel(this);

}

void MainWindow::msgListSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected);
    if (selected.indexes().isEmpty())
        return;

    QModelIndex index = selected.indexes().front();
    if (index.data(Imap::Mailbox::RoleMessageUid).isValid()) {
        updateMessageFlags(index);
        msgView->setMessage(index);
    }
}

void MainWindow::msgListActivated(const QModelIndex &index)
{
    Q_ASSERT(index.isValid());

    if (qApp->keyboardModifiers() & Qt::ShiftModifier || qApp->keyboardModifiers() & Qt::ControlModifier)
        return;

    if (! index.data(Imap::Mailbox::RoleMessageUid).isValid())
        return;

    if (index.column() != Imap::Mailbox::MsgListModel::SEEN) {
        msgView->setMessage(index);
        msgListTree->setCurrentIndex(index);
    }
}

void MainWindow::msgListClicked(const QModelIndex &index)
{
    Q_ASSERT(index.isValid());

    if (qApp->keyboardModifiers() & Qt::ShiftModifier || qApp->keyboardModifiers() & Qt::ControlModifier)
        return;

    if (! index.data(Imap::Mailbox::RoleMessageUid).isValid())
        return;

    if (index.column() == Imap::Mailbox::MsgListModel::SEEN) {
        QModelIndex translated;
        Imap::Mailbox::Model::realTreeItem(index, 0, &translated);
        if (!translated.data(Imap::Mailbox::RoleIsFetched).toBool())
            return;
        Imap::Mailbox::FlagsOperation flagOp = translated.data(Imap::Mailbox::RoleMessageIsMarkedRead).toBool() ?
                                               Imap::Mailbox::FLAG_REMOVE : Imap::Mailbox::FLAG_ADD;
        model->markMessagesRead(QModelIndexList() << translated, flagOp);
    }
}

void MainWindow::msgListDoubleClicked(const QModelIndex &index)
{
    Q_ASSERT(index.isValid());

    if (! index.data(Imap::Mailbox::RoleMessageUid).isValid())
        return;

    MessageView *newView = new MessageView(0);
    QModelIndex realIndex;
    const Imap::Mailbox::Model *realModel;
    Imap::Mailbox::TreeItemMessage *message = dynamic_cast<Imap::Mailbox::TreeItemMessage *>(
                Imap::Mailbox::Model::realTreeItem(index, &realModel, &realIndex));
    Q_ASSERT(message);
    Q_ASSERT(realModel == model);
    newView->setMessage(index);

    QScrollArea *widget = new QScrollArea();
    widget->setWidget(newView);
    widget->setWidgetResizable(true);
    widget->setWindowTitle(message->envelope(model).subject);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    widget->show();
}

void MainWindow::slotResizeMsgListColumns()
{
    for (int i = 0; i < msgListTree->header()->count(); ++i)
        msgListTree->resizeColumnToContents(i);
}

void MainWindow::showContextMenuMboxTree(const QPoint &position)
{
    QList<QAction *> actionList;
    if (mboxTree->indexAt(position).isValid()) {
        actionList.append(createChildMailbox);
        actionList.append(deleteCurrentMailbox);
        actionList.append(resyncMbox);
        actionList.append(reloadMboxList);

#ifdef XTUPLE_CONNECT
        actionList.append(xtIncludeMailboxInSync);
        xtIncludeMailboxInSync->setChecked(
            QSettings().value(Common::SettingsNames::xtSyncMailboxList).toStringList().contains(
                mboxTree->indexAt(position).data(Imap::Mailbox::RoleMailboxName).toString()));
#endif
    } else {
        actionList.append(createTopMailbox);
    }
    actionList.append(reloadAllMailboxes);
    QMenu::exec(actionList, mboxTree->mapToGlobal(position));
}

void MainWindow::showContextMenuMsgListTree(const QPoint &position)
{
    QList<QAction *> actionList;
    QModelIndex index = msgListTree->indexAt(position);
    if (index.isValid()) {
        updateMessageFlags(index);
        actionList.append(markAsRead);
        actionList.append(markAsDeleted);
        actionList.append(saveWholeMessage);
        actionList.append(viewMsgHeaders);
        actionList.append(releaseMessageData);
    }
    if (! actionList.isEmpty())
        QMenu::exec(actionList, msgListTree->mapToGlobal(position));
}

/** @short Ask for an updated list of mailboxes situated below the selected one

*/
void MainWindow::slotReloadMboxList()
{
    QModelIndexList indices = mboxTree->selectionModel()->selectedIndexes();
    for (QModelIndexList::const_iterator it = indices.begin(); it != indices.end(); ++it) {
        Q_ASSERT(it->isValid());
        if (it->column() != 0)
            continue;
        Imap::Mailbox::TreeItemMailbox *mbox = dynamic_cast<Imap::Mailbox::TreeItemMailbox *>(
                Imap::Mailbox::Model::realTreeItem(*it)
                                               );
        Q_ASSERT(mbox);
        mbox->rescanForChildMailboxes(model);
    }
}

/** @short Request a check for new messages in selected mailbox */
void MainWindow::slotResyncMbox()
{
    if (! model->isNetworkAvailable())
        return;

    QModelIndexList indices = mboxTree->selectionModel()->selectedIndexes();
    for (QModelIndexList::const_iterator it = indices.begin(); it != indices.end(); ++it) {
        Q_ASSERT(it->isValid());
        if (it->column() != 0)
            continue;
        Imap::Mailbox::TreeItemMailbox *mbox = dynamic_cast<Imap::Mailbox::TreeItemMailbox *>(
                Imap::Mailbox::Model::realTreeItem(*it)
                                               );
        Q_ASSERT(mbox);
        model->resyncMailbox(*it);
    }
}

void MainWindow::alertReceived(const QString &message)
{
    QMessageBox::warning(this, tr("IMAP Alert"), message);
}

void MainWindow::connectionError(const QString &message)
{
    if (QSettings().contains(Common::SettingsNames::imapMethodKey)) {
        QMessageBox::critical(this, tr("Connection Error"), message);
        // Show the IMAP logger -- maybe some user will take that as a hint that they shall include it in the bug report.
        // </joke>
        showImapLogger->setChecked(true);
    } else {
        // hack: this slot is called even on the first run with no configuration
        // We shouldn't have to worry about that, since the dialog is already scheduled for calling
        // -> do nothing
    }
    netOffline->trigger();
}

void MainWindow::cacheError(const QString &message)
{
    QMessageBox::critical(this, tr("IMAP Cache Error"),
                          tr("The caching subsystem managing a cache of the data already "
                             "downloaded from the IMAP server is having troubles. "
                             "All caching will be disabled.\n\n%1").arg(message));
    if (model)
        model->setCache(new Imap::Mailbox::MemoryCache(model, QString()));
}

void MainWindow::networkPolicyOffline()
{
    netOffline->setChecked(true);
    netExpensive->setChecked(false);
    netOnline->setChecked(false);
    updateActionsOnlineOffline(false);
    networkIndicator->setDefaultAction(netOffline);
    statusBar()->showMessage(tr("Offline"), 0);
}

void MainWindow::networkPolicyExpensive()
{
    netOffline->setChecked(false);
    netExpensive->setChecked(true);
    netOnline->setChecked(false);
    updateActionsOnlineOffline(true);
    networkIndicator->setDefaultAction(netExpensive);
}

void MainWindow::networkPolicyOnline()
{
    netOffline->setChecked(false);
    netExpensive->setChecked(false);
    netOnline->setChecked(true);
    updateActionsOnlineOffline(true);
    networkIndicator->setDefaultAction(netOnline);
}

void MainWindow::slotShowSettings()
{
    SettingsDialog *dialog = new SettingsDialog();
    if (dialog->exec() == QDialog::Accepted) {
        // FIXME: wipe cache in case we're moving between servers
        nukeModels();
        setupModels();
        connectModelActions();
    }
}

void MainWindow::authenticationRequested()
{
    QSettings s;
    QString user = s.value(Common::SettingsNames::imapUserKey).toString();
    QString pass = s.value(Common::SettingsNames::imapPassKey).toString();
    if (m_ignoreStoredPassword || pass.isEmpty()) {
        bool ok;
        pass = QInputDialog::getText(this, tr("Password"),
                                     tr("Please provide password for %1").arg(user),
                                     QLineEdit::Password, QString::null, &ok);
        if (ok) {
            model->setImapUser(user);
            model->setImapPassword(pass);
        } else {
            model->unsetImapPassword();
        }
    } else {
        model->setImapUser(user);
        model->setImapPassword(pass);
    }
}

void MainWindow::authenticationFailed(const QString &message)
{
    m_ignoreStoredPassword = true;
    QMessageBox::warning(this, tr("Login Failed"), message);
}

void MainWindow::sslErrors(const QList<QSslCertificate> &certificateChain, const QList<QSslError> &errors)
{
    QSettings s;
    QByteArray lastKnownCertPem = s.value(Common::SettingsNames::imapSslPemCertificate).toByteArray();
    QList<QSslCertificate> lastKnownCerts = lastKnownCertPem.isEmpty() ?
                QList<QSslCertificate>() :
                QSslCertificate::fromData(lastKnownCertPem, QSsl::Pem);
    if (!certificateChain.isEmpty()) {
        if (!lastKnownCerts.isEmpty()) {
            if (certificateChain == lastKnownCerts) {
                // It's the same certificate as the last time; we should accept that
                model->setSslPolicy(certificateChain, errors, true);
                return;
            }
        }
    }

    QString message;
    QString title;
    Imap::Mailbox::CertificateUtils::IconType icon;

    Imap::Mailbox::CertificateUtils::formatSslState(certificateChain, lastKnownCerts, lastKnownCertPem, errors,
                                                    &title, &message, &icon);

    if (QMessageBox(static_cast<QMessageBox::Icon>(icon), title, message, QMessageBox::Yes | QMessageBox::No, this).exec() == QMessageBox::Yes) {
        if (!certificateChain.isEmpty()) {
            QByteArray buf;
            Q_FOREACH(const QSslCertificate &cert, certificateChain) {
                buf.append(cert.toPem());
            }
            s.setValue(Common::SettingsNames::imapSslPemCertificate, buf);
        }
        model->setSslPolicy(certificateChain, errors, true);
    } else {
        model->setSslPolicy(certificateChain, errors, false);
    }
}

void MainWindow::nukeModels()
{
    msgView->setEmpty();
    mboxTree->setModel(0);
    msgListTree->setModel(0);
    allTree->setModel(0);
    taskTree->setModel(0);
    prettyMsgListModel->deleteLater();
    prettyMsgListModel = 0;
    threadingMsgListModel->deleteLater();
    threadingMsgListModel = 0;
    msgListModel->deleteLater();
    msgListModel = 0;
    mboxModel->deleteLater();
    mboxModel = 0;
    prettyMboxModel->deleteLater();
    prettyMboxModel = 0;
    model->deleteLater();
    model = 0;
    autoCompletionModel->deleteLater();
    autoCompletionModel = 0;
}

void MainWindow::slotComposeMail()
{
    invokeComposeDialog();
}

void MainWindow::handleMarkAsRead(bool value)
{
    QModelIndexList indices = msgListTree->selectionModel()->selectedIndexes();
    QModelIndexList translatedIndexes;
    for (QModelIndexList::const_iterator it = indices.begin(); it != indices.end(); ++it) {
        Q_ASSERT(it->isValid());
        if (it->column() != 0)
            continue;
        if (!it->data(Imap::Mailbox::RoleMessageUid).isValid())
            continue;
        QModelIndex translated;
        Imap::Mailbox::Model::realTreeItem(*it, 0, &translated);
        translatedIndexes << translated;
    }
    if (translatedIndexes.isEmpty()) {
        qDebug() << "Model::handleMarkAsRead: no valid messages";
    } else {
        if (value)
            model->markMessagesRead(translatedIndexes, Imap::Mailbox::FLAG_ADD);
        else
            model->markMessagesRead(translatedIndexes, Imap::Mailbox::FLAG_REMOVE);
    }
}

void MainWindow::slotNextUnread()
{
    QModelIndex current = msgListTree->currentIndex();

    bool wrapped = false;
    while (current.isValid()) {
        if (!current.data(Imap::Mailbox::RoleMessageIsMarkedRead).toBool() && msgListTree->currentIndex() != current) {
            msgView->setMessage(current);
            msgListTree->setCurrentIndex(current);
            return;
        }

        QModelIndex child = current.child(0, 0);
        if (child.isValid()) {
            current = child;
            continue;
        }

        QModelIndex sibling = current.sibling(current.row() + 1, 0);
        if (sibling.isValid()) {
            current = sibling;
            continue;
        }

        current = current.parent();
        if (!current.isValid())
            break;
        current = current.sibling(current.row() + 1, 0);

        if (!current.isValid() && !wrapped) {
            wrapped = true;
            current = msgListTree->model()->index(0, 0);
        }
    }
}

void MainWindow::slotPreviousUnread()
{
    QModelIndex current = msgListTree->currentIndex();

    bool wrapped = false;
    while (current.isValid()) {
        if (!current.data(Imap::Mailbox::RoleMessageIsMarkedRead).toBool() && msgListTree->currentIndex() != current) {
            msgView->setMessage(current);
            msgListTree->setCurrentIndex(current);
            return;
        }

        QModelIndex candidate = current.sibling(current.row() - 1, 0);
        while (candidate.isValid() && current.model()->hasChildren(candidate)) {
            candidate = candidate.child(current.model()->rowCount(candidate) - 1, 0);
            Q_ASSERT(candidate.isValid());
        }

        if (candidate.isValid()) {
            current = candidate;
        } else {
            current = current.parent();
        }
        if (!current.isValid() && !wrapped) {
            wrapped = true;
            while (msgListTree->model()->hasChildren(current)) {
                current = msgListTree->model()->index(msgListTree->model()->rowCount(current) - 1, 0, current);
            }
        }
    }
}

void MainWindow::handleMarkAsDeleted(bool value)
{
    QModelIndexList indices = msgListTree->selectionModel()->selectedIndexes();
    QModelIndexList translatedIndexes;
    for (QModelIndexList::const_iterator it = indices.begin(); it != indices.end(); ++it) {
        Q_ASSERT(it->isValid());
        if (it->column() != 0)
            continue;
        if (!it->data(Imap::Mailbox::RoleMessageUid).isValid())
            continue;
        QModelIndex translated;
        Imap::Mailbox::Model::realTreeItem(*it, 0, &translated);
        translatedIndexes << translated;
    }
    if (translatedIndexes.isEmpty()) {
        qDebug() << "Model::handleMarkAsDeleted: no valid messages";
    } else {
        if (value)
            model->markMessagesDeleted(translatedIndexes, Imap::Mailbox::FLAG_ADD);
        else
            model->markMessagesDeleted(translatedIndexes, Imap::Mailbox::FLAG_REMOVE);
    }
}

void MainWindow::slotExpunge()
{
    model->expungeMailbox(msgListModel->currentMailbox());
}

void MainWindow::slotCreateMailboxBelowCurrent()
{
    createMailboxBelow(mboxTree->currentIndex());
}

void MainWindow::slotCreateTopMailbox()
{
    createMailboxBelow(QModelIndex());
}

void MainWindow::createMailboxBelow(const QModelIndex &index)
{
    Imap::Mailbox::TreeItemMailbox *mboxPtr = index.isValid() ?
            dynamic_cast<Imap::Mailbox::TreeItemMailbox *>(
                Imap::Mailbox::Model::realTreeItem(index)) :
            0;

    Ui::CreateMailboxDialog ui;
    QDialog *dialog = new QDialog(this);
    ui.setupUi(dialog);

    dialog->setWindowTitle(mboxPtr ?
                           tr("Create a Subfolder of %1").arg(mboxPtr->mailbox()) :
                           tr("Create a Top-level Mailbox"));

    if (dialog->exec() == QDialog::Accepted) {
        QStringList parts;
        if (mboxPtr)
            parts << mboxPtr->mailbox();
        parts << ui.mailboxName->text();
        if (ui.otherMailboxes->isChecked())
            parts << QString();
        QString targetName = parts.join(mboxPtr ? mboxPtr->separator() : QString());   // FIXME: top-level separator
        model->createMailbox(targetName);
    }
}

void MainWindow::slotDeleteCurrentMailbox()
{
    if (! mboxTree->currentIndex().isValid())
        return;

    Imap::Mailbox::TreeItemMailbox *mailbox = dynamic_cast<Imap::Mailbox::TreeItemMailbox *>(
                Imap::Mailbox::Model::realTreeItem(mboxTree->currentIndex()));
    Q_ASSERT(mailbox);

    if (QMessageBox::question(this, tr("Delete Mailbox"),
                              tr("Are you sure to delete mailbox %1?").arg(mailbox->mailbox()),
                              QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        model->deleteMailbox(mailbox->mailbox());
    }
}

void MainWindow::updateMessageFlags()
{
    updateMessageFlags(msgListTree->currentIndex());
}

void MainWindow::updateMessageFlags(const QModelIndex &index)
{
    markAsRead->setChecked(index.data(Imap::Mailbox::RoleMessageIsMarkedRead).toBool());
    markAsDeleted->setChecked(index.data(Imap::Mailbox::RoleMessageIsMarkedDeleted).toBool());
}

void MainWindow::updateActionsOnlineOffline(bool online)
{
    reloadMboxList->setEnabled(online);
    resyncMbox->setEnabled(online);
    expunge->setEnabled(online);
    createChildMailbox->setEnabled(online);
    createTopMailbox->setEnabled(online);
    deleteCurrentMailbox->setEnabled(online);
    markAsDeleted->setEnabled(online);
    markAsRead->setEnabled(online);
    showImapCapabilities->setEnabled(online);
}

void MainWindow::scrollMessageUp()
{
    area->ensureVisible(0, 0, 0, 0);
}

void MainWindow::slotReplyTo()
{
    msgView->reply(this, MessageView::REPLY_SENDER_ONLY);
}

void MainWindow::slotReplyAll()
{
    msgView->reply(this, MessageView::REPLY_ALL);
}

void MainWindow::slotComposeMailUrl(const QUrl &url)
{
    Q_ASSERT(url.scheme().toLower() == QLatin1String("mailto"));

    QList<QPair<QString,QString> > recipients;
    recipients << qMakePair<QString,QString>(QString(), url.path());
    invokeComposeDialog(QString(), QString(), recipients);
}

void MainWindow::invokeComposeDialog(const QString &subject, const QString &body,
                                     const QList<QPair<QString,QString> > &recipients)
{
    QSettings s;
    ComposeWidget *w = new ComposeWidget(this, autoCompletionModel);
    w->setData(QString::fromAscii("%1 <%2>").arg(
                   s.value(Common::SettingsNames::realNameKey).toString(),
                   s.value(Common::SettingsNames::addressKey).toString()),
               recipients, subject, body);
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    Util::centerWidgetOnScreen(w);
    w->show();
}

void MainWindow::slotMailboxDeleteFailed(const QString &mailbox, const QString &msg)
{
    QMessageBox::warning(this, tr("Can't delete mailbox"),
                         tr("Deleting mailbox \"%1\" failed with the following message:\n%2").arg(mailbox, msg));
}

void MainWindow::slotMailboxCreateFailed(const QString &mailbox, const QString &msg)
{
    QMessageBox::warning(this, tr("Can't create mailbox"),
                         tr("Creating mailbox \"%1\" failed with the following message:\n%2").arg(mailbox, msg));
}

void MainWindow::showConnectionStatus(QObject *parser, Imap::ConnectionState state)
{
    Q_UNUSED(parser);
    using namespace Imap;
    QString message = connectionStateToString(state);
    enum { DURATION = 10000 };
    bool transient = false;

    switch (state) {
    case CONN_STATE_AUTHENTICATED:
    case CONN_STATE_SELECTED:
        transient = true;
        break;
    default:
        // only the stuff above is transient
        break;
    }
    statusBar()->showMessage(message, transient ? DURATION : 0);
}

void MainWindow::slotShowAboutTrojita()
{
    QMessageBox::about(this, trUtf8("About Trojitá"),
                       trUtf8("<p>This is <b>Trojitá</b>, a Qt IMAP e-mail client</p>"
                              "<p>Copyright &copy; 2006-2011 Jan Kundrát &lt;jkt@flaska.net&gt;</p>"
                              "<p>More information at http://trojita.flaska.net/</p>"
                              "<p>You are using version %1.</p>").arg(
                           QApplication::applicationVersion()));
}

void MainWindow::slotDonateToTrojita()
{
    QDesktopServices::openUrl(QString::fromAscii("http://sourceforge.net/donate/index.php?group_id=339456"));
}

void MainWindow::slotSaveCurrentMessageBody()
{
    QModelIndexList indices = msgListTree->selectionModel()->selectedIndexes();
    for (QModelIndexList::const_iterator it = indices.begin(); it != indices.end(); ++it) {
        Q_ASSERT(it->isValid());
        if (it->column() != 0)
            continue;
        if (! it->data(Imap::Mailbox::RoleMessageUid).isValid())
            continue;
        QModelIndex messageIndex;
        Imap::Mailbox::TreeItemMessage *message = dynamic_cast<Imap::Mailbox::TreeItemMessage *>(
                    Imap::Mailbox::Model::realTreeItem(*it, 0, &messageIndex)
                );
        Q_ASSERT(message);

        Imap::Network::MsgPartNetAccessManager *netAccess = new Imap::Network::MsgPartNetAccessManager(this);
        netAccess->setModelMessage(message->toIndex(model));
        Imap::Network::FileDownloadManager *fileDownloadManager =
            new Imap::Network::FileDownloadManager(this, netAccess, messageIndex.child(0, Imap::Mailbox::TreeItem::OFFSET_HEADER));
        // FIXME: change from "header" into "whole message"
        connect(fileDownloadManager, SIGNAL(succeeded()), fileDownloadManager, SLOT(deleteLater()));
        connect(fileDownloadManager, SIGNAL(transferError(QString)), fileDownloadManager, SLOT(deleteLater()));
        connect(fileDownloadManager, SIGNAL(fileNameRequested(QString *)),
                this, SLOT(slotDownloadMessageFileNameRequested(QString *)));
        connect(fileDownloadManager, SIGNAL(transferError(QString)),
                this, SLOT(slotDownloadMessageTransferError(QString)));
        connect(fileDownloadManager, SIGNAL(destroyed()), netAccess, SLOT(deleteLater()));
        fileDownloadManager->slotDownloadNow();
    }
}

void MainWindow::slotDownloadMessageTransferError(const QString &errorString)
{
    QMessageBox::critical(this, tr("Can't save message"),
                          tr("Unable to save the attachment. Error:\n%1").arg(errorString));
}

void MainWindow::slotDownloadMessageFileNameRequested(QString *fileName)
{
    *fileName = QFileDialog::getSaveFileName(this, tr("Save Message"),
                *fileName, QString(),
                0, QFileDialog::HideNameFilterDetails
                                            );
}

void MainWindow::slotViewMsgHeaders()
{
    QModelIndexList indices = msgListTree->selectionModel()->selectedIndexes();
    for (QModelIndexList::const_iterator it = indices.begin(); it != indices.end(); ++it) {
        Q_ASSERT(it->isValid());
        if (it->column() != 0)
            continue;
        if (! it->data(Imap::Mailbox::RoleMessageUid).isValid())
            continue;
        QModelIndex messageIndex;
        Imap::Mailbox::Model::realTreeItem(*it, 0, &messageIndex);

        Imap::Network::MsgPartNetAccessManager *netAccess = new Imap::Network::MsgPartNetAccessManager(this);
        netAccess->setModelMessage(messageIndex);

        QScrollArea *area = new QScrollArea();
        area->setWidgetResizable(true);
        SimplePartWidget *headers = new SimplePartWidget(0, netAccess, messageIndex.model()->index(0, Imap::Mailbox::TreeItem::OFFSET_HEADER, messageIndex));
        area->setWidget(headers);
        area->setAttribute(Qt::WA_DeleteOnClose);
        connect(area, SIGNAL(destroyed()), headers, SLOT(deleteLater()));
        connect(area, SIGNAL(destroyed()), netAccess, SLOT(deleteLater()));
        area->show();
        // FIXME: add an event filter for scrolling...
    }
}

#ifdef XTUPLE_CONNECT
void MainWindow::slotXtSyncCurrentMailbox()
{
    QModelIndex index = mboxTree->currentIndex();
    if (! index.isValid())
        return;

    QString mailbox = index.data(Imap::Mailbox::RoleMailboxName).toString();
    QSettings s;
    QStringList mailboxes = s.value(Common::SettingsNames::xtSyncMailboxList).toStringList();
    if (xtIncludeMailboxInSync->isChecked()) {
        if (! mailboxes.contains(mailbox)) {
            mailboxes.append(mailbox);
        }
    } else {
        mailboxes.removeAll(mailbox);
    }
    s.setValue(Common::SettingsNames::xtSyncMailboxList, mailboxes);
    QSettings(QSettings::UserScope, QString::fromAscii("xTuple.com"), QString::fromAscii("xTuple")).setValue(Common::SettingsNames::xtSyncMailboxList, mailboxes);
    prettyMboxModel->xtConnectStatusChanged(index);
}
#endif

void MainWindow::slotScrollToUnseenMessage(const QModelIndex &mailbox, const QModelIndex &message)
{
    // FIXME: unit tests
    Q_ASSERT(msgListModel);
    Q_ASSERT(msgListTree);
    if (model->realTreeItem(mailbox) != msgListModel->currentMailbox())
        return;

    // So, we have an index from Model, and have to map it through unspecified number of proxies here...
    QList<QAbstractProxyModel *> stack;
    QAbstractItemModel *tempModel = msgListModel;
    while (QAbstractProxyModel *proxy = qobject_cast<QAbstractProxyModel *>(tempModel)) {
        stack.insert(0, proxy);
        tempModel = proxy->sourceModel();
    }
    QModelIndex targetIndex = message;
    Q_FOREACH(QAbstractProxyModel *proxy, stack) {
        targetIndex = proxy->mapFromSource(targetIndex);
    }

    // ...now, we can scroll, using the translated index
    msgListTree->scrollTo(targetIndex);
}

void MainWindow::slotReleaseSelectedMessage()
{
    QModelIndex index = msgListTree->currentIndex();
    if (! index.isValid())
        return;
    if (! index.data(Imap::Mailbox::RoleMessageUid).isValid())
        return;

    model->releaseMessageData(index);
}

void MainWindow::slotThreadMsgList()
{
    // We want to save user's preferences and not override them with "threading disabled" when the server
    // doesn't report them, like in initial greetings. That's why we have to check for isEnabled() here.
    const bool useThreading = actionThreadMsgList->isChecked();
    if (useThreading && actionThreadMsgList->isEnabled()) {
        threadingMsgListModel->setSourceModel(msgListModel);
        prettyMsgListModel->setSourceModel(threadingMsgListModel);
        msgListTree->setRootIsDecorated(true);
    } else {
        prettyMsgListModel->setSourceModel(msgListModel);
        threadingMsgListModel->setSourceModel(0);
        msgListTree->setRootIsDecorated(false);
    }
    QSettings().setValue(Common::SettingsNames::guiMsgListShowThreading, QVariant(useThreading));
}

void MainWindow::slotHideRead()
{
    const bool hideRead = actionHideRead->isChecked();
    prettyMsgListModel->setHideRead(hideRead);
    QSettings().setValue(Common::SettingsNames::guiMsgListHideRead, QVariant(hideRead));
}

void MainWindow::slotCapabilitiesUpdated(const QStringList &capabilities)
{
    const QStringList supportedCapabilities = Imap::Mailbox::ThreadingMsgListModel::supportedCapabilities();
    Q_FOREACH(const QString &capability, capabilities) {
        if (supportedCapabilities.contains(capability)) {
            actionThreadMsgList->setEnabled(true);
            if (actionThreadMsgList->isChecked())
                slotThreadMsgList();
            return;
        }
    }
    actionThreadMsgList->setEnabled(false);
}

void MainWindow::slotShowImapInfo()
{
    QString caps;
    Q_FOREACH(const QString &cap, model->capabilities()) {
        caps += tr("<li>%1</li>\n").arg(cap);
    }

    QString idString;
    if (!model->serverId().isEmpty() && model->capabilities().contains(QLatin1String("ID"))) {
        QMap<QByteArray,QByteArray> serverId = model->serverId();

#define IMAP_ID_FIELD(Var, Name) bool has_##Var = serverId.contains(Name); \
    QString Var = has_##Var ? Qt::escape(QString::fromAscii(serverId[Name])) : tr("Unknown");
        IMAP_ID_FIELD(serverName, "name");
        IMAP_ID_FIELD(serverVersion, "version");
        IMAP_ID_FIELD(os, "os");
        IMAP_ID_FIELD(osVersion, "os-version");
        IMAP_ID_FIELD(vendor, "vendor");
        IMAP_ID_FIELD(supportUrl, "support-url");
        IMAP_ID_FIELD(address, "address");
        IMAP_ID_FIELD(date, "date");
        IMAP_ID_FIELD(command, "command");
        IMAP_ID_FIELD(arguments, "arguments");
        IMAP_ID_FIELD(environment, "environment");
#undef IMAP_ID_FIELD
        if (has_serverName) {
            idString = tr("<p>");
            if (has_serverVersion)
                idString += tr("Server: %1 %2").arg(serverName, serverVersion);
            else
                idString += tr("Server: %1").arg(serverName);

            if (has_vendor) {
                idString += tr(" (%1)").arg(vendor);
            }
            if (has_os) {
                if (has_osVersion)
                    idString += tr(" on %1 %2").arg(os, osVersion);
                else
                    idString += tr(" on %1").arg(os);
            }
            idString += tr("</p>");
        } else {
            idString = tr("<p>The IMAP server did not return usable information about itself.</p>");
        }
        QString fullId;
        for (QMap<QByteArray,QByteArray>::const_iterator it = serverId.constBegin(); it != serverId.constEnd(); ++it) {
            fullId += tr("<li>%1: %2</li>").arg(Qt::escape(QString::fromAscii(it.key())), Qt::escape(QString::fromAscii(it.value())));
        }
        idString += tr("<ul>%1</ul>").arg(fullId);
    } else {
        idString = tr("<p>The server has not provided information about its software version.</p>");
    }

    QMessageBox::information(this, tr("IMAP Server Information"),
                             tr("%1"
                                "<p>The following capabilities are currently advertised:</p>\n"
                                "<ul>\n%2</ul>").arg(idString, caps));
}

QSize MainWindow::sizeHint() const
{
    return QSize(1150, 980);
}

void MainWindow::slotUpdateWindowTitle()
{
    if (Imap::Mailbox::TreeItemMailbox *mailbox = msgListModel->currentMailbox()) {
        Imap::Mailbox::TreeItemMsgList *list = dynamic_cast<Imap::Mailbox::TreeItemMsgList*>(mailbox->child(0, model));
        Q_ASSERT(list);
        if (list->unreadMessageCount(model)) {
            setWindowTitle(trUtf8("%1 — %2 unread — Trojitá").arg(mailbox->mailbox(),
                                                                  QString::number(list->unreadMessageCount(model))));
        } else {
            setWindowTitle(trUtf8("%1 — Trojitá").arg(mailbox->mailbox()));
        }
    } else {
        setWindowTitle(trUtf8("Trojitá"));
    }
}

}


