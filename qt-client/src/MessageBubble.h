#ifndef CHAT_GUI_CLIENT_MESSAGEBUBBLE_H
#define CHAT_GUI_CLIENT_MESSAGEBUBBLE_H

#include <QString>
#include <QWidget>
#include <QtGlobal>

class QLabel;

class MessageBubble : public QWidget
{
  Q_OBJECT

 public:
  enum class Kind
  {
    Mine,
    Other,
    System,
    Error
  };

  explicit MessageBubble(const QString& sender,
                         const QString& message,
                         const QString& room,
                         Kind kind,
                         qint64 createdAt = 0,
                         QWidget* parent = nullptr);

 private:
  QString kindName() const;

  QLabel* senderLabel_;
  QLabel* messageLabel_;
  QLabel* timeLabel_;
  Kind kind_;
};

#endif  // CHAT_GUI_CLIENT_MESSAGEBUBBLE_H
