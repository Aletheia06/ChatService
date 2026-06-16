#include "MainWindow.h"

#include "ChatClient.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(ChatClient* client, QWidget* parent)
  : QMainWindow(parent),
    client_(client),
    usersList_(nullptr),
    roomsList_(nullptr),
    chatHistory_(nullptr),
    messageEdit_(nullptr),
    sendButton_(nullptr),
    refreshUsersButton_(nullptr),
    createRoomButton_(nullptr),
    joinRoomButton_(nullptr),
    leaveRoomButton_(nullptr),
    chatTitleLabel_(nullptr),
    statusLabel_(nullptr),
    currentMode_(ChatMode::None)
{
  buildUi();
  connectSignals();
  setWindowTitle("Chat Client - " + client_->username());
  statusLabel_->setText("Connected as " + client_->username());
  client_->requestUsers();
}

void MainWindow::refreshUsers()
{
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
  client_->joinRoom(room);
}

void MainWindow::leaveRoom()
{
  const QString room = selectedRoom();
  if (room.isEmpty())
  {
    QMessageBox::information(this, "Leave Room", "Select a room first.");
    return;
  }

  pendingLeaveRoom_ = room;
  client_->leaveRoom(room);
}

void MainWindow::sendCurrentMessage()
{
  const QString text = messageEdit_->text().trimmed();
  if (text.isEmpty())
  {
    QMessageBox::information(this, "Send Message", "Message cannot be empty.");
    return;
  }

  if (currentMode_ == ChatMode::Private)
  {
    if (currentTarget_.isEmpty())
    {
      QMessageBox::information(this, "Private Message", "Select an online user first.");
      return;
    }
    client_->sendPrivateMessage(currentTarget_, text);
    appendHistory("[Me -> " + currentTarget_ + "] " + text);
    messageEdit_->clear();
    return;
  }

  if (currentMode_ == ChatMode::Room)
  {
    if (currentTarget_.isEmpty())
    {
      QMessageBox::information(this, "Room Message", "Select a room first.");
      return;
    }
    client_->sendRoomMessage(currentTarget_, text);
    appendHistory("[Me @ " + currentTarget_ + "] " + text);
    messageEdit_->clear();
    return;
  }

  QMessageBox::information(this, "Send Message", "Select an online user or room first.");
}

void MainWindow::selectPrivateChat()
{
  QListWidgetItem* item = usersList_->currentItem();
  if (item == nullptr)
  {
    return;
  }
  roomsList_->clearSelection();
  setCurrentChat(ChatMode::Private, item->text());
}

void MainWindow::selectRoomChat()
{
  QListWidgetItem* item = roomsList_->currentItem();
  if (item == nullptr)
  {
    return;
  }
  usersList_->clearSelection();
  setCurrentChat(ChatMode::Room, item->text());
}

void MainWindow::updateUsers(const QStringList& users)
{
  usersList_->clear();
  usersList_->addItems(users);
  statusLabel_->setText("Online users refreshed.");
}

void MainWindow::showInfo(const QString& message)
{
  statusLabel_->setText(message);
  if (message == "room created" && !pendingCreateRoom_.isEmpty())
  {
    pendingJoinRoom_ = pendingCreateRoom_;
    pendingCreateRoom_.clear();
    statusLabel_->setText("Room created. Joining...");
    client_->joinRoom(pendingJoinRoom_);
  }
  else if (message == "joined room" && !pendingJoinRoom_.isEmpty())
  {
    addRoomIfMissing(pendingJoinRoom_);
    pendingJoinRoom_.clear();
  }
  else if (message == "left room" && !pendingLeaveRoom_.isEmpty())
  {
    removeRoom(pendingLeaveRoom_);
    pendingLeaveRoom_.clear();
  }
}

void MainWindow::showError(const QString& message)
{
  statusLabel_->setText("Error: " + message);
  appendHistory("[Error] " + message);
  pendingCreateRoom_.clear();
  pendingJoinRoom_.clear();
  pendingLeaveRoom_.clear();
}

void MainWindow::handlePrivateMessage(const QString& from, const QString& message)
{
  appendHistory("[Private] " + from + ": " + message);
}

void MainWindow::handleRoomMessage(const QString& room, const QString& from, const QString& message)
{
  if (from == client_->username())
  {
    return;
  }
  appendHistory("[Room " + room + "] " + from + ": " + message);
}

void MainWindow::handleDisconnected()
{
  statusLabel_->setText("Disconnected from server.");
  appendHistory("[System] Disconnected from server.");
}

void MainWindow::buildUi()
{
  resize(900, 600);

  usersList_ = new QListWidget(this);
  roomsList_ = new QListWidget(this);
  refreshUsersButton_ = new QPushButton("Refresh Users", this);
  createRoomButton_ = new QPushButton("Create Room", this);
  joinRoomButton_ = new QPushButton("Join Room", this);
  leaveRoomButton_ = new QPushButton("Leave Room", this);

  QVBoxLayout* leftLayout = new QVBoxLayout;
  leftLayout->addWidget(new QLabel("Online Users", this));
  leftLayout->addWidget(usersList_);
  leftLayout->addWidget(refreshUsersButton_);
  leftLayout->addSpacing(12);
  leftLayout->addWidget(new QLabel("Rooms", this));
  leftLayout->addWidget(roomsList_);
  leftLayout->addWidget(createRoomButton_);
  leftLayout->addWidget(joinRoomButton_);
  leftLayout->addWidget(leaveRoomButton_);

  QWidget* leftPanel = new QWidget(this);
  leftPanel->setLayout(leftLayout);

  chatTitleLabel_ = new QLabel("Select a user or room", this);
  chatHistory_ = new QTextBrowser(this);
  messageEdit_ = new QLineEdit(this);
  messageEdit_->setPlaceholderText("Type a message");
  sendButton_ = new QPushButton("Send", this);
  statusLabel_ = new QLabel(this);
  statusLabel_->setWordWrap(true);

  QHBoxLayout* inputLayout = new QHBoxLayout;
  inputLayout->addWidget(messageEdit_, 1);
  inputLayout->addWidget(sendButton_);

  QVBoxLayout* centerLayout = new QVBoxLayout;
  centerLayout->addWidget(chatTitleLabel_);
  centerLayout->addWidget(chatHistory_, 1);
  centerLayout->addLayout(inputLayout);
  centerLayout->addWidget(statusLabel_);

  QWidget* centerPanel = new QWidget(this);
  centerPanel->setLayout(centerLayout);

  QSplitter* splitter = new QSplitter(this);
  splitter->addWidget(leftPanel);
  splitter->addWidget(centerPanel);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes(QList<int>() << 240 << 660);

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
  connect(client_, &ChatClient::connectionError, this, &MainWindow::showError);
  connect(client_, &ChatClient::privateMessageReceived, this, &MainWindow::handlePrivateMessage);
  connect(client_, &ChatClient::roomMessageReceived, this, &MainWindow::handleRoomMessage);
  connect(client_, &ChatClient::disconnected, this, &MainWindow::handleDisconnected);
}

void MainWindow::setCurrentChat(ChatMode mode, const QString& name)
{
  currentMode_ = mode;
  currentTarget_ = name;
  if (mode == ChatMode::Private)
  {
    chatTitleLabel_->setText("Private: " + name);
  }
  else if (mode == ChatMode::Room)
  {
    chatTitleLabel_->setText("Room: " + name);
  }
  else
  {
    chatTitleLabel_->setText("Select a user or room");
  }
}

void MainWindow::addRoomIfMissing(const QString& room)
{
  if (roomsList_->findItems(room, Qt::MatchExactly).isEmpty())
  {
    roomsList_->addItem(room);
  }
}

void MainWindow::removeRoom(const QString& room)
{
  const QList<QListWidgetItem*> items = roomsList_->findItems(room, Qt::MatchExactly);
  for (QListWidgetItem* item : items)
  {
    delete roomsList_->takeItem(roomsList_->row(item));
  }
  if (currentMode_ == ChatMode::Room && currentTarget_ == room)
  {
    setCurrentChat(ChatMode::None, QString());
  }
}

void MainWindow::appendHistory(const QString& text)
{
  chatHistory_->append(text.toHtmlEscaped());
}

QString MainWindow::selectedRoom() const
{
  QListWidgetItem* item = roomsList_->currentItem();
  return item == nullptr ? QString() : item->text();
}
