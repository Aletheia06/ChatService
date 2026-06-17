#include "MainWindow.h"

#include "ChatClient.h"
#include "ClientConfig.h"
#include "Protocol.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace
{

constexpr int kValueRole = Qt::UserRole;
constexpr int kConversationTypeRole = Qt::UserRole + 1;
constexpr int kConversationIdRole = Qt::UserRole + 2;

QLabel* createSectionLabel(const QString& text, QWidget* parent)
{
  QLabel* label = new QLabel(text, parent);
  label->setObjectName("sectionLabel");
  return label;
}

QPushButton* createSidebarButton(const QString& text, QWidget* parent)
{
  QPushButton* button = new QPushButton(text, parent);
  button->setProperty("variant", "secondary");
  return button;
}

QListWidgetItem* findListItemByValue(QListWidget* list,
                                     const QString& value,
                                     int role = kValueRole)
{
  for (int row = 0; row < list->count(); ++row)
  {
    QListWidgetItem* item = list->item(row);
    if (item != nullptr && item->data(role).toString() == value)
    {
      return item;
    }
  }
  return nullptr;
}

void refreshStyle(QWidget* widget)
{
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  widget->update();
}

QString unreadPrefix(int unreadCount)
{
  return unreadCount > 0 ? QString("(%1) ").arg(unreadCount) : QString();
}

QString userItemText(const QString& username, int unreadCount)
{
  return unreadPrefix(unreadCount) + "@ " + username;
}

QString roomItemText(const QString& room, int unreadCount)
{
  return unreadPrefix(unreadCount) + "# " + room;
}

int typeRoleValue(MainWindow::ConversationType type)
{
  switch (type)
  {
    case MainWindow::ConversationType::Private:
      return 1;
    case MainWindow::ConversationType::Room:
      return 2;
    case MainWindow::ConversationType::None:
      return 0;
  }
  return 0;
}

MainWindow::ConversationType conversationTypeFromRole(int value)
{
  if (value == 1)
  {
    return MainWindow::ConversationType::Private;
  }
  if (value == 2)
  {
    return MainWindow::ConversationType::Room;
  }
  return MainWindow::ConversationType::None;
}

qint64 currentSeconds()
{
  return QDateTime::currentSecsSinceEpoch();
}

}  // namespace

MainWindow::MainWindow(ChatClient* client, QWidget* parent)
  : QMainWindow(parent),
    client_(client),
    usernameLabel_(nullptr),
    connectionStatusLabel_(nullptr),
    usersList_(nullptr),
    roomsList_(nullptr),
    recentConversationsList_(nullptr),
    messageScrollArea_(nullptr),
    messageContainer_(nullptr),
    messageLayout_(nullptr),
    messageEdit_(nullptr),
    sendButton_(nullptr),
    refreshUsersButton_(nullptr),
    createRoomButton_(nullptr),
    joinRoomButton_(nullptr),
    leaveRoomButton_(nullptr),
    chatTitleLabel_(nullptr),
    actionStatusLabel_(nullptr),
    currentType_(ConversationType::None),
    localMessageSequence_(0)
{
  buildUi();
  connectSignals();
  setWindowTitle("Chat Client - " + client_->username());
  usernameLabel_->setText(client_->username());
  setConnectionStatus("Connected", "connected");
  setActionStatus("Connected as " + client_->username());
  client_->requestUsers();
}

void MainWindow::refreshUsers()
{
  setActionStatus("Refreshing online users...");
  client_->requestUsers();
}

void MainWindow::createRoom()
{
  bool ok = false;
  const QString room = QInputDialog::getText(this,
                                             "Create Room",
                                             "Room name",
                                             QLineEdit::Normal,
                                             QString(),
                                             &ok).trimmed();
  if (!ok)
  {
    return;
  }
  if (room.isEmpty())
  {
    QMessageBox::warning(this, "Create Room", "Room name cannot be empty.");
    return;
  }

  pendingCreateRoom_ = room;
  setActionStatus("Creating room " + room + "...");
  client_->createRoom(room);
}

void MainWindow::joinRoom()
{
  bool ok = false;
  const QString room = QInputDialog::getText(this,
                                             "Join Room",
                                             "Room name",
                                             QLineEdit::Normal,
                                             QString(),
                                             &ok).trimmed();
  if (!ok)
  {
    return;
  }
  if (room.isEmpty())
  {
    QMessageBox::warning(this, "Join Room", "Room name cannot be empty.");
    return;
  }

  pendingJoinRoom_ = room;
  setActionStatus("Joining room " + room + "...");
  client_->joinRoom(room);
}

void MainWindow::leaveRoom()
{
  const QString room = selectedRoom();
  if (room.isEmpty())
  {
    setActionStatus("Select a room first.");
    appendSystemMessage("Select a room before leaving.");
    return;
  }

  pendingLeaveRoom_ = room;
  setActionStatus("Leaving room " + room + "...");
  client_->leaveRoom(room);
}

void MainWindow::sendCurrentMessage()
{
  const QString text = messageEdit_->text().trimmed();
  if (text.isEmpty())
  {
    setActionStatus("Type a message before sending.");
    return;
  }

  if (currentType_ == ConversationType::Private)
  {
    if (currentConversationId_.isEmpty())
    {
      setActionStatus("Select an online user first.");
      return;
    }

    ChatMessage message;
    message.sender = client_->username();
    message.receiver = currentConversationId_;
    message.content = text;
    message.createdAt = currentSeconds();
    message.kind = MessageBubble::Kind::Mine;
    message.dedupeKey = nextLocalMessageKey();

    client_->sendPrivateMessage(currentConversationId_, text);
    addMessageToConversation(ConversationType::Private,
                             currentConversationId_,
                             message,
                             true,
                             false);
    messageEdit_->clear();
    messageEdit_->setFocus();
    setActionStatus("Message sent.");
    return;
  }

  if (currentType_ == ConversationType::Room)
  {
    if (currentConversationId_.isEmpty())
    {
      setActionStatus("Select a room first.");
      return;
    }

    ChatMessage message;
    message.sender = client_->username();
    message.room = currentConversationId_;
    message.content = text;
    message.createdAt = currentSeconds();
    message.kind = MessageBubble::Kind::Mine;
    message.dedupeKey = nextLocalMessageKey();

    client_->sendRoomMessage(currentConversationId_, text);
    addMessageToConversation(ConversationType::Room,
                             currentConversationId_,
                             message,
                             true,
                             false);
    messageEdit_->clear();
    messageEdit_->setFocus();
    setActionStatus("Message sent.");
    return;
  }

  setActionStatus("Select an online user or room first.");
}

void MainWindow::selectPrivateChat()
{
  const QList<QListWidgetItem*> selected = usersList_->selectedItems();
  if (selected.isEmpty())
  {
    return;
  }

  selectConversation(ConversationType::Private,
                     selected.first()->data(kValueRole).toString());
}

void MainWindow::selectRoomChat()
{
  const QList<QListWidgetItem*> selected = roomsList_->selectedItems();
  if (selected.isEmpty())
  {
    return;
  }

  selectConversation(ConversationType::Room,
                     selected.first()->data(kValueRole).toString());
}

void MainWindow::selectRecentConversation()
{
  const QList<QListWidgetItem*> selected =
      recentConversationsList_->selectedItems();
  if (selected.isEmpty())
  {
    return;
  }

  QListWidgetItem* item = selected.first();
  selectConversation(
      conversationTypeFromRole(item->data(kConversationTypeRole).toInt()),
      item->data(kConversationIdRole).toString());
}

void MainWindow::updateUsers(const QStringList& users)
{
  usersList_->clear();
  for (const QString& user : users)
  {
    addUserIfMissing(user);
  }

  if (currentType_ == ConversationType::Private &&
      !currentConversationId_.isEmpty())
  {
    addUserIfMissing(currentConversationId_);
  }
  setActionStatus("Online users refreshed.");
}

void MainWindow::showInfo(const QString& message)
{
  setActionStatus(message);

  if (message == "room created" && !pendingCreateRoom_.isEmpty())
  {
    pendingJoinRoom_ = pendingCreateRoom_;
    pendingCreateRoom_.clear();
    setActionStatus("Room created. Joining...");
    client_->joinRoom(pendingJoinRoom_);
    return;
  }

  if (message == "joined room" && !pendingJoinRoom_.isEmpty())
  {
    const QString room = pendingJoinRoom_;
    addRoomIfMissing(room);
    pendingJoinRoom_.clear();
    selectConversation(ConversationType::Room, room);
    appendSystemMessage("Joined room: " + room);
    return;
  }

  if (message == "left room" && !pendingLeaveRoom_.isEmpty())
  {
    const QString room = pendingLeaveRoom_;
    removeRoom(room);
    pendingLeaveRoom_.clear();
    appendSystemMessage("Left room: " + room);
    return;
  }

  if (message == "private message sent" || message == "private message saved")
  {
    return;
  }

  appendSystemMessage(message);
}

void MainWindow::showError(const QString& message)
{
  setActionStatus("Error: " + message);
  appendErrorMessage(message);
  pendingCreateRoom_.clear();
  pendingJoinRoom_.clear();
  pendingLeaveRoom_.clear();
}

void MainWindow::handleConnectionError(const QString& message)
{
  setConnectionStatus("Connection error", "error");
  showError(message);
}

void MainWindow::handlePrivateMessage(const QString& from,
                                      const QString& message,
                                      qint64 id,
                                      qint64 createdAt)
{
  addUserIfMissing(from);

  ChatMessage chatMessage;
  chatMessage.id = id;
  chatMessage.sender = from;
  chatMessage.receiver = client_->username();
  chatMessage.content = message;
  chatMessage.createdAt = createdAt > 0 ? createdAt : currentSeconds();
  chatMessage.kind = MessageBubble::Kind::Other;
  if (id > 0)
  {
    chatMessage.dedupeKey = "db:" + QString::number(id);
  }

  addMessageToConversation(ConversationType::Private,
                           from,
                           chatMessage,
                           true,
                           true);
}

void MainWindow::handleRoomMessage(const QString& room,
                                   const QString& from,
                                   const QString& message,
                                   qint64 id,
                                   qint64 createdAt)
{
  if (from == client_->username())
  {
    return;
  }

  addRoomIfMissing(room);

  ChatMessage chatMessage;
  chatMessage.id = id;
  chatMessage.sender = from;
  chatMessage.room = room;
  chatMessage.content = message;
  chatMessage.createdAt = createdAt > 0 ? createdAt : currentSeconds();
  chatMessage.kind = MessageBubble::Kind::Other;
  if (id > 0)
  {
    chatMessage.dedupeKey = "db:" + QString::number(id);
  }

  addMessageToConversation(ConversationType::Room,
                           room,
                           chatMessage,
                           true,
                           true);
}

void MainWindow::handlePrivateHistory(const QString& peer,
                                      const QJsonArray& messages)
{
  mergePrivateHistory(peer, messages);
}

void MainWindow::handleRoomHistory(const QString& room,
                                   const QJsonArray& messages)
{
  mergeRoomHistory(room, messages);
}

void MainWindow::handleDisconnected()
{
  setConnectionStatus("Disconnected", "disconnected");
  setActionStatus("Disconnected from server.");
  messageEdit_->setEnabled(false);
  sendButton_->setEnabled(false);
  appendSystemMessage("Disconnected from server.");
}

void MainWindow::buildUi()
{
  resize(1120, 720);

  usernameLabel_ = new QLabel(this);
  usernameLabel_->setObjectName("userNameLabel");
  connectionStatusLabel_ = new QLabel("Disconnected", this);
  connectionStatusLabel_->setObjectName("connectionStatus");
  connectionStatusLabel_->setProperty("status", "disconnected");

  usersList_ = new QListWidget(this);
  roomsList_ = new QListWidget(this);
  recentConversationsList_ = new QListWidget(this);
  refreshUsersButton_ = createSidebarButton("Refresh Users", this);
  createRoomButton_ = createSidebarButton("Create Room", this);
  joinRoomButton_ = createSidebarButton("Join Room", this);
  leaveRoomButton_ = createSidebarButton("Leave Room", this);

  QVBoxLayout* leftLayout = new QVBoxLayout;
  leftLayout->setContentsMargins(18, 20, 18, 20);
  leftLayout->setSpacing(10);
  leftLayout->addWidget(createSectionLabel("Current User", this));
  leftLayout->addWidget(usernameLabel_);
  leftLayout->addWidget(connectionStatusLabel_);
  leftLayout->addSpacing(10);
  leftLayout->addWidget(createSectionLabel("Online Users", this));
  leftLayout->addWidget(usersList_, 1);
  leftLayout->addWidget(refreshUsersButton_);
  leftLayout->addSpacing(10);
  leftLayout->addWidget(createSectionLabel("Rooms", this));
  leftLayout->addWidget(roomsList_, 1);
  leftLayout->addWidget(createRoomButton_);
  leftLayout->addWidget(joinRoomButton_);
  leftLayout->addWidget(leaveRoomButton_);
  leftLayout->addSpacing(10);
  leftLayout->addWidget(createSectionLabel("Recent Conversations", this));
  leftLayout->addWidget(recentConversationsList_, 1);

  QWidget* leftPanel = new QWidget(this);
  leftPanel->setObjectName("sidebar");
  leftPanel->setAttribute(Qt::WA_StyledBackground, true);
  leftPanel->setMinimumWidth(280);
  leftPanel->setMaximumWidth(360);
  leftPanel->setLayout(leftLayout);

  chatTitleLabel_ = new QLabel("Select a user or room", this);
  chatTitleLabel_->setObjectName("chatTitle");

  messageContainer_ = new QWidget(this);
  messageContainer_->setObjectName("messageContainer");
  messageContainer_->setAttribute(Qt::WA_StyledBackground, true);
  messageLayout_ = new QVBoxLayout(messageContainer_);
  messageLayout_->setContentsMargins(16, 16, 16, 16);
  messageLayout_->setSpacing(10);
  messageLayout_->setAlignment(Qt::AlignTop);
  messageContainer_->setLayout(messageLayout_);

  messageScrollArea_ = new QScrollArea(this);
  messageScrollArea_->setObjectName("messageScrollArea");
  messageScrollArea_->setAttribute(Qt::WA_StyledBackground, true);
  messageScrollArea_->setWidgetResizable(true);
  messageScrollArea_->setFrameShape(QFrame::NoFrame);
  messageScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  messageScrollArea_->setWidget(messageContainer_);

  messageEdit_ = new QLineEdit(this);
  messageEdit_->setPlaceholderText("Type a message...");
  messageEdit_->setEnabled(false);
  sendButton_ = new QPushButton("Send", this);
  sendButton_->setEnabled(false);
  actionStatusLabel_ = new QLabel(this);
  actionStatusLabel_->setObjectName("actionStatus");
  actionStatusLabel_->setWordWrap(true);

  QHBoxLayout* inputLayout = new QHBoxLayout;
  inputLayout->setContentsMargins(0, 0, 0, 0);
  inputLayout->setSpacing(10);
  inputLayout->addWidget(messageEdit_, 1);
  inputLayout->addWidget(sendButton_);

  QVBoxLayout* centerLayout = new QVBoxLayout;
  centerLayout->setContentsMargins(20, 20, 20, 20);
  centerLayout->setSpacing(12);
  centerLayout->addWidget(chatTitleLabel_);
  centerLayout->addWidget(messageScrollArea_, 1);
  centerLayout->addLayout(inputLayout);
  centerLayout->addWidget(actionStatusLabel_);

  QWidget* centerPanel = new QWidget(this);
  centerPanel->setObjectName("chatPanel");
  centerPanel->setAttribute(Qt::WA_StyledBackground, true);
  centerPanel->setLayout(centerLayout);

  QSplitter* splitter = new QSplitter(this);
  splitter->setHandleWidth(1);
  splitter->addWidget(leftPanel);
  splitter->addWidget(centerPanel);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes(QList<int>() << 320 << 800);

  setCentralWidget(splitter);
}

void MainWindow::connectSignals()
{
  connect(refreshUsersButton_, &QPushButton::clicked, this, &MainWindow::refreshUsers);
  connect(createRoomButton_, &QPushButton::clicked, this, &MainWindow::createRoom);
  connect(joinRoomButton_, &QPushButton::clicked, this, &MainWindow::joinRoom);
  connect(leaveRoomButton_, &QPushButton::clicked, this, &MainWindow::leaveRoom);
  connect(sendButton_, &QPushButton::clicked, this, &MainWindow::sendCurrentMessage);
  connect(messageEdit_, &QLineEdit::returnPressed, this, &MainWindow::sendCurrentMessage);
  connect(usersList_, &QListWidget::itemSelectionChanged, this, &MainWindow::selectPrivateChat);
  connect(roomsList_, &QListWidget::itemSelectionChanged, this, &MainWindow::selectRoomChat);
  connect(recentConversationsList_, &QListWidget::itemSelectionChanged, this, &MainWindow::selectRecentConversation);

  connect(client_, &ChatClient::usersUpdated, this, &MainWindow::updateUsers);
  connect(client_, &ChatClient::infoMessage, this, &MainWindow::showInfo);
  connect(client_, &ChatClient::errorMessage, this, &MainWindow::showError);
  connect(client_, &ChatClient::connectionError, this, &MainWindow::handleConnectionError);
  connect(client_, &ChatClient::privateMessageReceived, this, &MainWindow::handlePrivateMessage);
  connect(client_, &ChatClient::roomMessageReceived, this, &MainWindow::handleRoomMessage);
  connect(client_, &ChatClient::privateHistoryReceived, this, &MainWindow::handlePrivateHistory);
  connect(client_, &ChatClient::roomHistoryReceived, this, &MainWindow::handleRoomHistory);
  connect(client_, &ChatClient::disconnected, this, &MainWindow::handleDisconnected);
}

void MainWindow::selectConversation(ConversationType type, const QString& id)
{
  if (type == ConversationType::None || id.isEmpty())
  {
    currentType_ = ConversationType::None;
    currentConversationId_.clear();
    chatTitleLabel_->setText("Select a user or room");
    messageEdit_->setEnabled(false);
    sendButton_->setEnabled(false);
    clearRenderedMessages();
    setActionStatus("Select a conversation.");
    return;
  }

  ConversationState& conversation = ensureConversation(type, id);
  conversation.unreadCount = 0;
  currentType_ = type;
  currentConversationId_ = id;
  updateConversationListItem(&conversation);
  updateConversationBadges(&conversation);

  {
    QSignalBlocker usersBlocker(usersList_);
    QSignalBlocker roomsBlocker(roomsList_);
    QSignalBlocker recentBlocker(recentConversationsList_);
    usersList_->clearSelection();
    roomsList_->clearSelection();
    recentConversationsList_->clearSelection();

    if (type == ConversationType::Private)
    {
      QListWidgetItem* userItem = addUserIfMissing(id);
      if (userItem != nullptr)
      {
        usersList_->setCurrentItem(userItem);
      }
    }
    else if (type == ConversationType::Room)
    {
      addRoomIfMissing(id);
      QListWidgetItem* roomItem = findListItemByValue(roomsList_, id);
      if (roomItem != nullptr)
      {
        roomsList_->setCurrentItem(roomItem);
      }
    }

    if (conversation.recentItem != nullptr)
    {
      recentConversationsList_->setCurrentItem(conversation.recentItem);
    }
  }

  chatTitleLabel_->setText(conversationTitle(type, id));
  messageEdit_->setEnabled(true);
  sendButton_->setEnabled(true);
  requestHistoryIfNeeded(&conversation);
  renderCurrentConversation();
  setActionStatus(conversationTitle(type, id));
}

MainWindow::ConversationState& MainWindow::ensureConversation(
    ConversationType type,
    const QString& id)
{
  const QString key = conversationKey(type, id);
  ConversationState& conversation = conversations_[key];
  if (conversation.type == ConversationType::None)
  {
    conversation.type = type;
    conversation.id = id;
  }
  updateConversationListItem(&conversation);
  return conversation;
}

MainWindow::ConversationState* MainWindow::conversationForKey(
    const QString& key)
{
  QHash<QString, ConversationState>::iterator it = conversations_.find(key);
  if (it == conversations_.end())
  {
    return nullptr;
  }
  return &it.value();
}

MainWindow::ConversationState* MainWindow::currentConversation()
{
  return conversationForKey(currentConversationKey());
}

QString MainWindow::conversationKey(ConversationType type,
                                    const QString& id) const
{
  if (type == ConversationType::Private)
  {
    return "private:" + id;
  }
  if (type == ConversationType::Room)
  {
    return "room:" + id;
  }
  return QString();
}

QString MainWindow::currentConversationKey() const
{
  return conversationKey(currentType_, currentConversationId_);
}

QString MainWindow::conversationTitle(ConversationType type,
                                      const QString& id) const
{
  if (type == ConversationType::Private)
  {
    return "Private chat with " + id;
  }
  if (type == ConversationType::Room)
  {
    return "# " + id;
  }
  return "Select a user or room";
}

QString MainWindow::recentLabel(const ConversationState& conversation) const
{
  const QString base =
      conversation.type == ConversationType::Private
          ? "Private: " + conversation.id
          : "# " + conversation.id;
  return unreadPrefix(conversation.unreadCount) + base;
}

bool MainWindow::addMessageToConversation(ConversationType type,
                                          const QString& id,
                                          const ChatMessage& message,
                                          bool renderIfActive,
                                          bool markUnread)
{
  ConversationState& conversation = ensureConversation(type, id);
  ChatMessage stored = message;
  if (stored.createdAt <= 0)
  {
    stored.createdAt = currentSeconds();
  }
  if (stored.dedupeKey.isEmpty())
  {
    stored.dedupeKey = stored.id > 0 ? "db:" + QString::number(stored.id)
                                     : nextLocalMessageKey();
  }
  if (conversation.messageKeys.contains(stored.dedupeKey))
  {
    return false;
  }

  conversation.messageKeys.insert(stored.dedupeKey);
  conversation.messages.append(stored);
  std::sort(conversation.messages.begin(),
            conversation.messages.end(),
            [](const ChatMessage& left, const ChatMessage& right) {
              if (left.createdAt != right.createdAt)
              {
                return left.createdAt < right.createdAt;
              }
              if (left.id != right.id)
              {
                return left.id < right.id;
              }
              return left.dedupeKey < right.dedupeKey;
            });

  const bool active = conversationKey(type, id) == currentConversationKey();
  if (!active && markUnread)
  {
    ++conversation.unreadCount;
  }

  updateConversationListItem(&conversation);
  updateConversationBadges(&conversation);
  if (active && renderIfActive)
  {
    renderCurrentConversation();
    scrollChatToBottom();
  }
  return true;
}

void MainWindow::mergePrivateHistory(const QString& peer,
                                     const QJsonArray& messages)
{
  int added = 0;
  for (const QJsonValue& value : messages)
  {
    if (!value.isObject())
    {
      continue;
    }
    const QJsonObject object = value.toObject();

    ChatMessage message;
    message.id = Protocol::int64Value(object, "id");
    message.sender = Protocol::stringValue(object, "sender");
    message.receiver = Protocol::stringValue(object, "receiver");
    message.content = Protocol::stringValue(object, "content");
    message.createdAt = Protocol::int64Value(object, "created_at");
    message.kind = message.sender == client_->username()
                       ? MessageBubble::Kind::Mine
                       : MessageBubble::Kind::Other;
    if (message.id > 0)
    {
      message.dedupeKey = "db:" + QString::number(message.id);
    }

    if (!message.sender.isEmpty() && !message.content.isEmpty() &&
        addMessageToConversation(ConversationType::Private,
                                 peer,
                                 message,
                                 false,
                                 false))
    {
      ++added;
    }
  }

  if (currentConversationKey() == conversationKey(ConversationType::Private, peer))
  {
    renderCurrentConversation();
    scrollChatToBottom();
  }
  setActionStatus(QString("Loaded %1 private history message(s).").arg(added));
}

void MainWindow::mergeRoomHistory(const QString& room,
                                  const QJsonArray& messages)
{
  int added = 0;
  for (const QJsonValue& value : messages)
  {
    if (!value.isObject())
    {
      continue;
    }
    const QJsonObject object = value.toObject();

    ChatMessage message;
    message.id = Protocol::int64Value(object, "id");
    message.sender = Protocol::stringValue(object, "sender");
    message.room = Protocol::stringValue(object, "room");
    if (message.room.isEmpty())
    {
      message.room = room;
    }
    message.content = Protocol::stringValue(object, "content");
    message.createdAt = Protocol::int64Value(object, "created_at");
    message.kind = message.sender == client_->username()
                       ? MessageBubble::Kind::Mine
                       : MessageBubble::Kind::Other;
    if (message.id > 0)
    {
      message.dedupeKey = "db:" + QString::number(message.id);
    }

    if (!message.sender.isEmpty() && !message.content.isEmpty() &&
        addMessageToConversation(ConversationType::Room,
                                 room,
                                 message,
                                 false,
                                 false))
    {
      ++added;
    }
  }

  if (currentConversationKey() == conversationKey(ConversationType::Room, room))
  {
    renderCurrentConversation();
    scrollChatToBottom();
  }
  setActionStatus(QString("Loaded %1 room history message(s).").arg(added));
}

MainWindow::ChatMessage MainWindow::makeSystemMessage(
    const QString& text,
    MessageBubble::Kind kind)
{
  ChatMessage message;
  message.sender = kind == MessageBubble::Kind::Error ? "Error" : "System";
  message.content = text;
  message.createdAt = currentSeconds();
  message.kind = kind;
  message.dedupeKey = nextLocalMessageKey();
  return message;
}

QString MainWindow::nextLocalMessageKey()
{
  ++localMessageSequence_;
  return "local:" + QString::number(localMessageSequence_);
}

void MainWindow::renderCurrentConversation()
{
  clearRenderedMessages();
  ConversationState* conversation = currentConversation();
  if (conversation == nullptr)
  {
    return;
  }

  for (const ChatMessage& message : conversation->messages)
  {
    appendBubble(message);
  }
}

void MainWindow::clearRenderedMessages()
{
  while (QLayoutItem* item = messageLayout_->takeAt(0))
  {
    if (QWidget* widget = item->widget())
    {
      delete widget;
    }
    delete item;
  }
}

void MainWindow::appendBubble(const ChatMessage& message)
{
  QWidget* row = new QWidget(messageContainer_);
  row->setObjectName("messageRow");
  row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  QHBoxLayout* rowLayout = new QHBoxLayout(row);
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(0);

  QString sender = message.sender;
  if (message.kind == MessageBubble::Kind::Mine)
  {
    sender = currentType_ == ConversationType::Private && !message.receiver.isEmpty()
                 ? "Me -> " + message.receiver
                 : "Me";
  }

  const QString room =
      currentType_ == ConversationType::Room ? message.room : QString();
  MessageBubble* bubble = new MessageBubble(sender,
                                            message.content,
                                            room,
                                            message.kind,
                                            message.createdAt,
                                            row);

  if (message.kind == MessageBubble::Kind::Mine)
  {
    rowLayout->addStretch(1);
    rowLayout->addWidget(bubble);
  }
  else if (message.kind == MessageBubble::Kind::System ||
           message.kind == MessageBubble::Kind::Error)
  {
    rowLayout->addStretch(1);
    rowLayout->addWidget(bubble);
    rowLayout->addStretch(1);
  }
  else
  {
    rowLayout->addWidget(bubble);
    rowLayout->addStretch(1);
  }

  messageLayout_->addWidget(row);
}

void MainWindow::scrollChatToBottom()
{
  messageContainer_->updateGeometry();
  messageLayout_->activate();

  QTimer::singleShot(0, this, [this]() {
    messageContainer_->adjustSize();
    QScrollBar* scrollBar = messageScrollArea_->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());

    QTimer::singleShot(0, this, [this]() {
      QScrollBar* scrollBar = messageScrollArea_->verticalScrollBar();
      scrollBar->setValue(scrollBar->maximum());
    });
  });
}

void MainWindow::requestHistoryIfNeeded(ConversationState* conversation)
{
  if (conversation == nullptr || conversation->historyRequested)
  {
    return;
  }

  conversation->historyRequested = true;
  if (conversation->type == ConversationType::Private)
  {
    client_->requestPrivateHistory(conversation->id,
                                   ClientConfig::DEFAULT_HISTORY_LIMIT);
    setActionStatus("Loading private history...");
  }
  else if (conversation->type == ConversationType::Room)
  {
    client_->requestRoomHistory(conversation->id,
                                ClientConfig::DEFAULT_HISTORY_LIMIT);
    setActionStatus("Loading room history...");
  }
}

void MainWindow::updateConversationListItem(ConversationState* conversation)
{
  if (conversation == nullptr || conversation->type == ConversationType::None)
  {
    return;
  }

  const QString key = conversationKey(conversation->type, conversation->id);
  QListWidgetItem* item = conversation->recentItem;
  if (item == nullptr || recentConversationsList_->row(item) < 0)
  {
    item = findListItemByValue(recentConversationsList_, key);
  }
  if (item == nullptr)
  {
    item = new QListWidgetItem;
    recentConversationsList_->insertItem(0, item);
  }
  else
  {
    const int row = recentConversationsList_->row(item);
    if (row > 0)
    {
      recentConversationsList_->takeItem(row);
      recentConversationsList_->insertItem(0, item);
    }
  }

  item->setText(recentLabel(*conversation));
  item->setData(kValueRole, key);
  item->setData(kConversationTypeRole, typeRoleValue(conversation->type));
  item->setData(kConversationIdRole, conversation->id);
  conversation->recentItem = item;
}

void MainWindow::updateConversationBadges(ConversationState* conversation)
{
  if (conversation == nullptr)
  {
    return;
  }

  if (conversation->recentItem != nullptr)
  {
    conversation->recentItem->setText(recentLabel(*conversation));
  }

  if (conversation->type == ConversationType::Private)
  {
    QListWidgetItem* item = findListItemByValue(usersList_, conversation->id);
    if (item != nullptr)
    {
      item->setText(userItemText(conversation->id, conversation->unreadCount));
    }
  }
  else if (conversation->type == ConversationType::Room)
  {
    QListWidgetItem* item = findListItemByValue(roomsList_, conversation->id);
    if (item != nullptr)
    {
      item->setText(roomItemText(conversation->id, conversation->unreadCount));
    }
  }
}

QListWidgetItem* MainWindow::addUserIfMissing(const QString& username)
{
  if (username.isEmpty() || username == client_->username())
  {
    return nullptr;
  }

  ConversationState* conversation =
      conversationForKey(conversationKey(ConversationType::Private, username));
  const int unreadCount = conversation == nullptr ? 0 : conversation->unreadCount;

  QListWidgetItem* existing = findListItemByValue(usersList_, username);
  if (existing != nullptr)
  {
    existing->setText(userItemText(username, unreadCount));
    return existing;
  }

  QListWidgetItem* item =
      new QListWidgetItem(userItemText(username, unreadCount), usersList_);
  item->setData(kValueRole, username);
  return item;
}

void MainWindow::addRoomIfMissing(const QString& room)
{
  if (room.isEmpty())
  {
    return;
  }

  ConversationState* conversation =
      conversationForKey(conversationKey(ConversationType::Room, room));
  const int unreadCount = conversation == nullptr ? 0 : conversation->unreadCount;

  QListWidgetItem* existing = findListItemByValue(roomsList_, room);
  if (existing != nullptr)
  {
    existing->setText(roomItemText(room, unreadCount));
    return;
  }

  QListWidgetItem* item =
      new QListWidgetItem(roomItemText(room, unreadCount), roomsList_);
  item->setData(kValueRole, room);
}

void MainWindow::removeRoom(const QString& room)
{
  while (QListWidgetItem* item = findListItemByValue(roomsList_, room))
  {
    delete roomsList_->takeItem(roomsList_->row(item));
  }

  if (currentType_ == ConversationType::Room && currentConversationId_ == room)
  {
    selectConversation(ConversationType::None, QString());
  }
}

void MainWindow::appendSystemMessage(const QString& text)
{
  if (currentType_ == ConversationType::None || currentConversationId_.isEmpty())
  {
    setActionStatus(text);
    return;
  }

  addMessageToConversation(currentType_,
                           currentConversationId_,
                           makeSystemMessage(text, MessageBubble::Kind::System),
                           true,
                           false);
}

void MainWindow::appendErrorMessage(const QString& text)
{
  if (currentType_ == ConversationType::None || currentConversationId_.isEmpty())
  {
    setActionStatus("Error: " + text);
    return;
  }

  addMessageToConversation(currentType_,
                           currentConversationId_,
                           makeSystemMessage(text, MessageBubble::Kind::Error),
                           true,
                           false);
}

void MainWindow::setConnectionStatus(const QString& text, const QString& state)
{
  connectionStatusLabel_->setText(text);
  connectionStatusLabel_->setProperty("status", state);
  refreshStyle(connectionStatusLabel_);
}

void MainWindow::setActionStatus(const QString& text)
{
  actionStatusLabel_->setText(text);
}

QString MainWindow::selectedRoom() const
{
  const QList<QListWidgetItem*> selected = roomsList_->selectedItems();
  if (!selected.isEmpty())
  {
    return selected.first()->data(kValueRole).toString();
  }

  if (currentType_ == ConversationType::Room)
  {
    return currentConversationId_;
  }

  return QString();
}
