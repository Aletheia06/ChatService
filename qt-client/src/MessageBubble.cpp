#include "MessageBubble.h"

#include <QDateTime>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

MessageBubble::MessageBubble(const QString& sender,
                             const QString& message,
                             const QString& room,
                             Kind kind,
                             QWidget* parent)
  : QWidget(parent),
    senderLabel_(nullptr),
    messageLabel_(nullptr),
    timeLabel_(nullptr),
    kind_(kind)
{
  const QString kindValue = kindName();
  QString title = sender;
  if (!room.isEmpty())
  {
    title += " @ " + room;
  }

  setObjectName("messageBubble");
  setProperty("kind", kindValue);
  setAttribute(Qt::WA_StyledBackground, true);
  setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
  setMaximumWidth(520);

  senderLabel_ = new QLabel(title, this);
  senderLabel_->setObjectName("messageBubbleSender");
  senderLabel_->setProperty("bubbleKind", kindValue);
  senderLabel_->setTextFormat(Qt::PlainText);

  messageLabel_ = new QLabel(message, this);
  messageLabel_->setObjectName("messageBubbleText");
  messageLabel_->setProperty("bubbleKind", kindValue);
  messageLabel_->setTextFormat(Qt::PlainText);
  messageLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  messageLabel_->setWordWrap(true);

  timeLabel_ = new QLabel(QDateTime::currentDateTime().toString("HH:mm"), this);
  timeLabel_->setObjectName("messageBubbleTime");
  timeLabel_->setProperty("bubbleKind", kindValue);
  timeLabel_->setAlignment(Qt::AlignRight);
  timeLabel_->setTextFormat(Qt::PlainText);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 8, 12, 8);
  layout->setSpacing(4);
  layout->addWidget(senderLabel_);
  layout->addWidget(messageLabel_);
  layout->addWidget(timeLabel_);
  setLayout(layout);
}

QString MessageBubble::kindName() const
{
  switch (kind_)
  {
    case Kind::Mine:
      return QStringLiteral("mine");
    case Kind::Other:
      return QStringLiteral("other");
    case Kind::System:
      return QStringLiteral("system");
    case Kind::Error:
      return QStringLiteral("error");
  }

  return QStringLiteral("other");
}
