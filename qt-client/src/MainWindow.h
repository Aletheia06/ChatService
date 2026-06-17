#ifndef CHAT_GUI_CLIENT_MAINWINDOW_H
#define CHAT_GUI_CLIENT_MAINWINDOW_H

#include "MessageBubble.h"

#include <QHash>
#include <QJsonArray>
#include <QMainWindow>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtCore/Qt>

class ChatClient;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

class MainWindow : public QMainWindow
{
  Q_OBJECT

 public:
  enum class ConversationType
  {
    None,
    Private,
    Room
  };

  explicit MainWindow(ChatClient* client, QWidget* parent = nullptr);

 private slots:
  void refreshUsers();
  void createRoom();
  void joinRoom();
  void leaveRoom();
  void sendCurrentMessage();
  void selectPrivateChat();
  void selectRoomChat();
  void selectRecentConversation();
  void updateUsers(const QStringList& users);
  void showInfo(const QString& message);
  void showError(const QString& message);
  void handleConnectionError(const QString& message);
  void handlePrivateMessage(const QString& from,
                            const QString& message,
                            qint64 id,
                            qint64 createdAt);
  void handleRoomMessage(const QString& room,
                         const QString& from,
                         const QString& message,
                         qint64 id,
                         qint64 createdAt);
  void handlePrivateHistory(const QString& peer, const QJsonArray& messages);
  void handleRoomHistory(const QString& room, const QJsonArray& messages);
  void handleDisconnected();

 private:
  struct ChatMessage
  {
    qint64 id = 0;
    QString sender;
    QString receiver;
    QString room;
    QString content;
    qint64 createdAt = 0;
    MessageBubble::Kind kind = MessageBubble::Kind::Other;
    QString dedupeKey;
  };

  struct ConversationState
  {
    ConversationType type = ConversationType::None;
    QString id;
    QVector<ChatMessage> messages;
    QSet<QString> messageKeys;
    bool historyRequested = false;
    int unreadCount = 0;
    QListWidgetItem* recentItem = nullptr;
  };

  void buildUi();
  void connectSignals();
  void selectConversation(ConversationType type, const QString& id);
  ConversationState& ensureConversation(ConversationType type, const QString& id);
  ConversationState* conversationForKey(const QString& key);
  ConversationState* currentConversation();
  QString conversationKey(ConversationType type, const QString& id) const;
  QString currentConversationKey() const;
  QString conversationTitle(ConversationType type, const QString& id) const;
  QString recentLabel(const ConversationState& conversation) const;
  bool addMessageToConversation(ConversationType type,
                                const QString& id,
                                const ChatMessage& message,
                                bool renderIfActive,
                                bool markUnread);
  void mergePrivateHistory(const QString& peer, const QJsonArray& messages);
  void mergeRoomHistory(const QString& room, const QJsonArray& messages);
  ChatMessage makeSystemMessage(const QString& text, MessageBubble::Kind kind);
  QString nextLocalMessageKey();
  void renderCurrentConversation();
  void clearRenderedMessages();
  void appendBubble(const ChatMessage& message);
  void scrollChatToBottom();
  void requestHistoryIfNeeded(ConversationState* conversation);
  void updateConversationListItem(ConversationState* conversation);
  void updateConversationBadges(ConversationState* conversation);
  QListWidgetItem* addUserIfMissing(const QString& username);
  void addRoomIfMissing(const QString& room);
  void removeRoom(const QString& room);
  void appendSystemMessage(const QString& text);
  void appendErrorMessage(const QString& text);
  void setConnectionStatus(const QString& text, const QString& state);
  void setActionStatus(const QString& text);
  QString selectedRoom() const;

  ChatClient* client_;
  QLabel* usernameLabel_;
  QLabel* connectionStatusLabel_;
  QListWidget* usersList_;
  QListWidget* roomsList_;
  QListWidget* recentConversationsList_;
  QScrollArea* messageScrollArea_;
  QWidget* messageContainer_;
  QVBoxLayout* messageLayout_;
  QLineEdit* messageEdit_;
  QPushButton* sendButton_;
  QPushButton* refreshUsersButton_;
  QPushButton* createRoomButton_;
  QPushButton* joinRoomButton_;
  QPushButton* leaveRoomButton_;
  QLabel* chatTitleLabel_;
  QLabel* actionStatusLabel_;
  QHash<QString, ConversationState> conversations_;
  ConversationType currentType_;
  QString currentConversationId_;
  QString pendingCreateRoom_;
  QString pendingJoinRoom_;
  QString pendingLeaveRoom_;
  qint64 localMessageSequence_;
};

#endif  // CHAT_GUI_CLIENT_MAINWINDOW_H
