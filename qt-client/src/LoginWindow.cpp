#include "LoginWindow.h"

#include "ChatClient.h"
#include "ClientConfig.h"
#include "MainWindow.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

LoginWindow::LoginWindow(QWidget* parent)
  : QWidget(parent),
    hostEdit_(nullptr),
    portSpinBox_(nullptr),
    usernameEdit_(nullptr),
    advancedButton_(nullptr),
    advancedPanel_(nullptr),
    loginButton_(nullptr),
    statusLabel_(nullptr),
    client_(new ChatClient()),
    mainWindow_(nullptr)
{
  buildUi();

  connect(loginButton_, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
  connect(advancedButton_, &QPushButton::toggled, this, &LoginWindow::toggleAdvancedSettings);
  connect(client_, &ChatClient::connected, this, &LoginWindow::onSocketConnected);
  connect(client_, &ChatClient::loginSucceeded, this, &LoginWindow::onLoginSucceeded);
  connect(client_, &ChatClient::loginFailed, this, &LoginWindow::onLoginFailed);
  connect(client_, &ChatClient::connectionError, this, &LoginWindow::onConnectionError);
  connect(client_, &ChatClient::disconnected, this, &LoginWindow::onDisconnected);
  connect(client_, &ChatClient::infoMessage, this, &LoginWindow::setStatus);
}

LoginWindow::~LoginWindow()
{
  delete client_;
}

void LoginWindow::onLoginClicked()
{
  const QString host = hostEdit_->text().trimmed();
  const QString username = usernameEdit_->text().trimmed();

  if (host.isEmpty())
  {
    setStatus("Server host is required.");
    return;
  }

  if (username.isEmpty())
  {
    setStatus("Username is required.");
    return;
  }

  pendingUsername_ = username;
  setLoginEnabled(false);
  setStatus("Connecting...");
  client_->connectToServer(host, static_cast<quint16>(portSpinBox_->value()));
}

void LoginWindow::onSocketConnected()
{
  setStatus("Connected. Logging in...");
  client_->login(pendingUsername_);
}

void LoginWindow::onLoginSucceeded(const QString&)
{
  setStatus("Login succeeded.");
  mainWindow_ = new MainWindow(client_);
  client_->setParent(mainWindow_);
  client_ = nullptr;
  mainWindow_->show();
  close();
}

void LoginWindow::onLoginFailed(const QString& message)
{
  setStatus("Login failed: " + message);
  setLoginEnabled(true);
}

void LoginWindow::onConnectionError(const QString& message)
{
  setStatus("Connection error: " + message);
  setLoginEnabled(true);
}

void LoginWindow::onDisconnected()
{
  if (mainWindow_ == nullptr)
  {
    setStatus("Disconnected from server.");
    setLoginEnabled(true);
  }
}

void LoginWindow::setStatus(const QString& message)
{
  statusLabel_->setText(message);
}

void LoginWindow::toggleAdvancedSettings(bool checked)
{
  advancedPanel_->setVisible(checked);
  advancedButton_->setText(checked ? "Hide Advanced Settings" : "Advanced Settings");
}

void LoginWindow::buildUi()
{
  setObjectName("loginWindow");
  setWindowTitle("Chat Login");
  resize(390, 260);

  hostEdit_ = new QLineEdit(ClientConfig::DEFAULT_SERVER_HOST, this);
  portSpinBox_ = new QSpinBox(this);
  portSpinBox_->setRange(1, 65535);
  portSpinBox_->setValue(ClientConfig::DEFAULT_SERVER_PORT);
  usernameEdit_ = new QLineEdit(this);
  usernameEdit_->setPlaceholderText("alice");
  advancedButton_ = new QPushButton("Advanced Settings", this);
  advancedButton_->setCheckable(true);
  advancedButton_->setProperty("variant", "secondary");
  loginButton_ = new QPushButton("Login", this);
  statusLabel_ = new QLabel("Enter username.", this);
  statusLabel_->setObjectName("loginStatus");
  statusLabel_->setWordWrap(true);

  QLabel* titleLabel = new QLabel("Chat Login", this);
  titleLabel->setObjectName("appTitle");

  QFormLayout* form = new QFormLayout;
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(12);
  form->addRow("Username", usernameEdit_);

  QFormLayout* advancedForm = new QFormLayout;
  advancedForm->setContentsMargins(0, 0, 0, 0);
  advancedForm->setSpacing(12);
  advancedForm->addRow("Host", hostEdit_);
  advancedForm->addRow("Port", portSpinBox_);

  advancedPanel_ = new QWidget(this);
  advancedPanel_->setLayout(advancedForm);
  advancedPanel_->setVisible(false);

  QHBoxLayout* buttonRow = new QHBoxLayout;
  buttonRow->addWidget(advancedButton_);
  buttonRow->addStretch();
  buttonRow->addWidget(loginButton_);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(14);
  layout->addWidget(titleLabel);
  layout->addLayout(form);
  layout->addWidget(advancedPanel_);
  layout->addLayout(buttonRow);
  layout->addWidget(statusLabel_);
  setLayout(layout);
}

void LoginWindow::setLoginEnabled(bool enabled)
{
  loginButton_->setEnabled(enabled);
  advancedButton_->setEnabled(enabled);
  hostEdit_->setEnabled(enabled);
  portSpinBox_->setEnabled(enabled);
  usernameEdit_->setEnabled(enabled);
}
