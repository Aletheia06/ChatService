#include "MainWindow.h"

#include "ChatClient.h"
#include "MessageBubble.h"

#include <QFrame>
#include <QDebug>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace
{

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

QString userIndicator()
{
  return QString::fromUtf8("\342\200\242 ");
}

QListWidgetItem* findListItemByValue(QListWidget* list, const QString& value)
{
  for (int row = 0; row < list->count(); ++row)
  {
    QListWidgetItem* item = list->item(row);
    if (item != nullptr && item->data(Qt::UserRole).toString() == value)
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

}  // namespace

MainWindow::MainWindow(ChatClient* client, QWidget* parent)
  : QMainWindow(parent),
    client_(client),
    usernameLabel_(nullptr),
    connectionStatusLabel_(nullptr),
    usersList_(nullptr),
    roomsList_(nullptr),
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
    currentMode_(ChatMode::None)
{
  buildUi();
  connectSignals();
  setWindowTitle("Chat Client - " + client_->username());
  usernameLabel_->setText(client_->username());
  setConnectionStatus("Connected", "connected");
  setActionStatus("Connected as " + client_->username());
  appendSystemMessage("Connected as " + client_->username());
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
  const QString room = QInputDialog::getText(this, "Create Room", "Room name",
                                            QLineEdit::Normal, QString(), &ok).trimmed();
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
  const QString room = QInputDialog::getText(this, "Join Room", "Room name",
                                            QLineEdit::Normal, QString(), &ok).trimmed();
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

  if (currentMode_ == ChatMode::Private)
  {
    if (currentPrivateTarget_.isEmpty())
    {
      setActionStatus("Select an online user first.");
      appendSystemMessage("Select an online user before sending a private message.");
      return;
    }
    qDebug() << "GUI sending private message to target:" << currentPrivateTarget_;
    client_->sendPrivateMessage(currentPrivateTarget_, text);
    appendPrivateMessage(currentPrivateTarget_, text, true);
    messageEdit_->clear();
    messageEdit_->setFocus();
    setActionStatus("Message sent.");
    return;
  }

  if (currentMode_ == ChatMode::Room)
  {
    if (currentRoomName_.isEmpty())
    {
      setActionStatus("Select a room first.");
      appendSystemMessage("Select a room before sending a room message.");
      return;
    }
    client_->sendRoomMessage(currentRoomName_, text);
    appendRoomMessage(currentRoomName_, client_->username(), text, true);
    messageEdit_->clear();
    messageEdit_->setFocus();
    setActionStatus("Message sent.");
    return;
  }

  setActionStatus("Select an online user or room first.");
  appendSystemMessage("Select an online user or room before sending.");
}

void MainWindow::selectPrivateChat()
{
  const QList<QListWidgetItem*> selected = usersList_->selectedItems();
  if (selected.isEmpty())
  {
    return;
  }

  QListWidgetItem* item = selected.first();
  roomsList_->clearSelection();
  setCurrentChat(ChatMode::Private, item->data(Qt::UserRole).toString());
}

void MainWindow::selectRoomChat()
{
  const QList<QListWidgetItem*> selected = roomsList_->selectedItems();
  if (selected.isEmpty())
  {
    return;
  }

  QListWidgetItem* item = selected.first();
  usersList_->clearSelection();
  setCurrentChat(ChatMode::Room, item->data(Qt::UserRole).toString());
}

void MainWindow::updateUsers(const QStringList& users)
{
  usersList_->clear();
  for (const QString& user : users)
  {
    addUserIfMissing(user);
  }

  if (!currentPrivateTarget_.isEmpty())
  {
    addUserIfMissing(currentPrivateTarget_);
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
    appendSystemMessage("Room created. Joining room...");
    client_->joinRoom(pendingJoinRoom_);
    return;
  }

  if (message == "joined room" && !pendingJoinRoom_.isEmpty())
  {
    const QString room = pendingJoinRoom_;
    addRoomIfMissing(room);
    pendingJoinRoom_.clear();
    QListWidgetItem* item = findListItemByValue(roomsList_, room);
    if (item != nullptr)
    {
      roomsList_->setCurrentItem(item);
    }
    setCurrentChat(ChatMode::Room, room);
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

  if (message != "private message sent")
  {
    appendSystemMessage(message);
  }
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

void MainWindow::handlePrivateMessage(const QString& from, const QString& message)
{
  qDebug() << "GUI displaying incoming private message from:" << from;
  QListWidgetItem* item = addUserIfMissing(from);
  if ((currentMode_ == ChatMode::None ||
       (currentMode_ == ChatMode::Private && currentPrivateTarget_.isEmpty())) &&
      item != nullptr)
  {
    usersList_->setCurrentItem(item);
    setCurrentChat(ChatMode::Private, from);
  }

  appendPrivateMessage(from, message, false);
}

void MainWindow::handleRoomMessage(const QString& room, const QString& from, const QString& message)
{
  if (from == client_->username())
  {
    return;
  }
  appendRoomMessage(room, from, message, false);
}

void MainWindow::handleDisconnected()
{
  setConnectionStatus("Disconnected", "disconnected");
  setActionStatus("Disconnected from server.");
  appendSystemMessage("Disconnected from server.");
}

void MainWindow::buildUi()
{
  resize(1040, 700);

  usernameLabel_ = new QLabel(this);
  usernameLabel_->setObjectName("userNameLabel");
  connectionStatusLabel_ = new QLabel("Disconnected", this);
  connectionStatusLabel_->setObjectName("connectionStatus");
  connectionStatusLabel_->setProperty("status", "disconnected");

  usersList_ = new QListWidget(this);
  roomsList_ = new QListWidget(this);
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
  leftLayout->addSpacing(12);
  leftLayout->addWidget(createSectionLabel("Online Users", this));
  leftLayout->addWidget(usersList_, 1);
  leftLayout->addWidget(refreshUsersButton_);
  leftLayout->addSpacing(12);
  leftLayout->addWidget(createSectionLabel("Rooms", this));
  leftLayout->addWidget(roomsList_, 1);
  leftLayout->addWidget(createRoomButton_);
  leftLayout->addWidget(joinRoomButton_);
  leftLayout->addWidget(leaveRoomButton_);

  QWidget* leftPanel = new QWidget(this);
  leftPanel->setObjectName("sidebar");
  leftPanel->setAttribute(Qt::WA_StyledBackground, true);
  leftPanel->setMinimumWidth(260);
  leftPanel->setMaximumWidth(330);
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
  sendButton_ = new QPushButton("Send", this);
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
  splitter->setSizes(QList<int>() << 290 << 750);

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

  connect(client_, &ChatClient::usersUpdated, this, &MainWindow::updateUsers);
  connect(client_, &ChatClient::infoMessage, this, &MainWindow::showInfo);
  connect(client_, &ChatClient::errorMessage, this, &MainWindow::showError);
  connect(client_, &ChatClient::connectionError, this, &MainWindow::handleConnectionError);
  connect(client_, &ChatClient::privateMessageReceived, this, &MainWindow::handlePrivateMessage);
  connect(client_, &ChatClient::roomMessageReceived, this, &MainWindow::handleRoomMessage);
  connect(client_, &ChatClient::disconnected, this, &MainWindow::handleDisconnected);
}

void MainWindow::setCurrentChat(ChatMode mode, const QString& name)
{
  currentMode_ = mode;
  if (mode == ChatMode::Private)
  {
    currentPrivateTarget_ = name;
    currentRoomName_.clear();
    chatTitleLabel_->setText("Private chat with " + name);
    setActionStatus("Private chat selected: " + name);
  }
  else if (mode == ChatMode::Room)
  {
    currentRoomName_ = name;
    currentPrivateTarget_.clear();
    chatTitleLabel_->setText("# " + name);
    setActionStatus("Room selected: " + name);
  }
  else
  {
    currentPrivateTarget_.clear();
    currentRoomName_.clear();
    chatTitleLabel_->setText("Select a user or room");
    setActionStatus("Select a conversation.");
  }
}

QListWidgetItem* MainWindow::addUserIfMissing(const QString& username)
{
  if (username.isEmpty() || username == client_->username())
  {
    return nullptr;
  }

  QListWidgetItem* existing = findListItemByValue(usersList_, username);
  if (existing != nullptr)
  {
    return existing;
  }

  QListWidgetItem* item = new QListWidgetItem(userIndicator() + username, usersList_);
  item->setData(Qt::UserRole, username);
  return item;
}

void MainWindow::addRoomIfMissing(const QString& room)
{
  if (findListItemByValue(roomsList_, room) == nullptr)
  {
    QListWidgetItem* item = new QListWidgetItem("# " + room, roomsList_);
    item->setData(Qt::UserRole, room);
  }
}

void MainWindow::removeRoom(const QString& room)
{
  while (QListWidgetItem* item = findListItemByValue(roomsList_, room))
  {
    delete roomsList_->takeItem(roomsList_->row(item));
  }

  if (currentMode_ == ChatMode::Room && currentRoomName_ == room)
  {
    setCurrentChat(ChatMode::None, QString());
  }
}

void MainWindow::appendSystemMessage(const QString& text)
{
  appendBubble(new MessageBubble("System", text, QString(), MessageBubble::Kind::System, messageContainer_),
               Qt::AlignHCenter);
  scrollChatToBottom();
}

void MainWindow::appendErrorMessage(const QString& text)
{
  appendBubble(new MessageBubble("Error", text, QString(), MessageBubble::Kind::Error, messageContainer_),
               Qt::AlignHCenter);
  scrollChatToBottom();
}

void MainWindow::appendPrivateMessage(const QString& from, const QString& message, bool isMine)
{
  const QString sender = isMine ? "Me -> " + from : from;
  appendBubble(new MessageBubble(sender,
                                 message,
                                 QString(),
                                 isMine ? MessageBubble::Kind::Mine : MessageBubble::Kind::Other,
                                 messageContainer_),
               isMine ? Qt::AlignRight : Qt::AlignLeft);
  scrollChatToBottom();
}

void MainWindow::appendRoomMessage(const QString& room,
                                   const QString& from,
                                   const QString& message,
                                   bool isMine)
{
  const QString sender = isMine ? "Me" : from;
  appendBubble(new MessageBubble(sender,
                                 message,
                                 room,
                                 isMine ? MessageBubble::Kind::Mine : MessageBubble::Kind::Other,
                                 messageContainer_),
               isMine ? Qt::AlignRight : Qt::AlignLeft);
  scrollChatToBottom();
}

void MainWindow::appendBubble(QWidget* bubble, Qt::Alignment alignment)
{
  QWidget* row = new QWidget(messageContainer_);
  row->setObjectName("messageRow");
  row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  QHBoxLayout* rowLayout = new QHBoxLayout(row);
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->setSpacing(0);

  if (alignment & Qt::AlignRight)
  {
    rowLayout->addStretch(1);
    rowLayout->addWidget(bubble);
  }
  else if (alignment & Qt::AlignHCenter)
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
  if (selected.isEmpty())
  {
    return QString();
  }
  return selected.first()->data(Qt::UserRole).toString();
}
